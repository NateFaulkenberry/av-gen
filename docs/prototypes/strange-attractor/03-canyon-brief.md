# AV Gen — Strange Attractor Canyon
## Engine-Native Terrain, Audio-Reactive Geology, Digital Revelations, and Living Wildlife

## 1. Mission

Take the successful procedural canyon from the Strange Attractor experiment and turn it into a production-quality AV Gen scene using engine-native triangle geometry.

The existing 46-second, 1920×1080, 30 fps flythrough is the artistic reference. The canyon's terrain generation has produced some of the best landscape imagery in the project so far. The primary responsibility of this task is to preserve that achievement through conversion to the actual renderer, eliminate the current edge-distortion issues, and establish a robust foundation for a sophisticated audio-reactive environment.

Once the terrain conversion is successful, develop the canyon into a musical environment in which geological formations, terrain topology, birds, atmospheric elements, and potentially a mysterious riverside campsite respond to musical energy in distinct, intentional ways.

The artistic premise:

**A monumental natural canyon whose underlying mathematical reality occasionally reveals itself through the music.**

Most of the time, the landscape should look and feel like a beautiful, coherent place. At selected musical moments, its terrain reveals luminous triangular structures, flowing geometric contours, and mysterious symbols embedded in the rock. Wildlife provides a sense of scale and life. A distant campsite may hint at an unknown presence inhabiting the landscape.

This should feel like a handcrafted surrealist environment with a sophisticated visual language, not a generic game landscape, a conventional music visualizer, or a collection of unrelated audio-reactive effects.

## 2. Non-negotiable requirements

1. Preserve the existing canyon's recognizable silhouette, terrain composition, river corridor, canyon walls, lighting character, and successful camera path.
2. Use actual engine-native triangle geometry for the canyon terrain. The goal is to eliminate the edge distortion associated with the current rendering approach.
3. Keep the terrain static in world space initially. Audio should reveal, illuminate, and animate visual properties rather than deforming the canyon itself.
4. Make the digital effects operate on or derive from the real terrain geometry wherever practical.
5. Use AV Gen's existing audio-analysis, parameter modulation, scene, material, and rendering systems. Do not build a parallel audio engine or bespoke rendering architecture for this scene.
6. Implement audio responses with appropriate smoothing, normalization, hysteresis, and cooldowns. Avoid noisy flicker and frame-rate-dependent behavior.
7. Preserve the scene's natural beauty when all audio effects are disabled.
8. Target 1920×1080 at 30 fps on the project's intended live hardware as a soft performance goal. A compelling offline-rendering scene remains acceptable if the final quality cannot fit the live budget.
9. Keep the implementation focused. Do not turn this into a general-purpose terrain editor, a new procedural-world framework, or an elaborate wildlife simulation engine.
10. Do not introduce backwards-compatibility shims for abandoned experimental implementations unless a small, clean compatibility mechanism is genuinely required by the current architecture.
11. Do not begin the art expansion until the engine-native terrain has been verified against the reference flythrough.
12. Do not replace the successful terrain with a newly generated landscape merely because a new implementation is easier.

## 3. Begin with repository research

Before changing code, investigate the actual implementation in the repository.

Do not assume that the existing experiment is a conventional heightfield. A canyon can contain overhangs, near-vertical walls, complex river banks, multiple visible surface layers, and non-heightfield geometry. The correct conversion strategy depends on the representation that produced the current result.

### 3.1 Locate the source of truth

Find:

- The Strange Attractor experiment and its procedural terrain generator.
- The scene definition and the code or assets that configure the current flythrough.
- The terrain representation, including whether it is a heightfield, implicit surface, voxel surface, raymarched function, signed-distance field, generated mesh, or another representation.
- The source of the current edge distortion.
- Existing engine-native triangle mesh construction and upload APIs.
- Existing mesh/material, vertex/index buffer, scene-node, transform, and asset-loading abstractions.
- Existing procedural material and shader support.
- Existing water, reflection, sky, fog, bloom, and terrain-lighting implementations.
- Existing audio analysis, modulation routing, envelope, onset, and scene-parameter APIs.
- Existing animation, instancing, spline/path, and particle systems.
- Existing scene tests, render tests, capture tools, profiling tools, and offline rendering functionality.

Read the relevant architecture documents and existing architectural decision records. Follow the current project conventions rather than inventing abstractions that duplicate them.

### 3.2 Deep research requirement

Perform targeted research before selecting the mesh conversion and surface-shading approach.

Investigate at least:

- Triangulated irregular networks and mesh extraction from implicit surfaces.
- Marching cubes, marching tetrahedra, dual contouring, or other extraction techniques if the existing terrain is implicit or volumetric.
- Mesh sampling, adaptive tessellation, silhouette preservation, and error-controlled simplification.
- Vertex normals, triangle winding, boundary closure, mesh watertightness, and numerical precision.
- Triplanar procedural materials for steep terrain.
- Barycentric-coordinate wireframe rendering and its suitability for a WebGPU/WGSL renderer.
- Efficient rendering of repeated animated objects and small flocks.
- Audio-reactive visual systems that use continuous envelopes for broad behavior and transient events for discrete actions.

Use primary technical sources, engine documentation, relevant rendering papers, and practical implementations. Compare the approaches that actually fit this engine rather than selecting a technique based on novelty.

Useful starting references include:

- NVIDIA GPU Gems, procedural terrain generation: https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu
- NVIDIA GPU Gems, geometry clipmaps and terrain LOD: https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry
- AMD GPUOpen, procedural generation: https://gpuopen.com/learn/work_graphs_mesh_nodes/work_graphs_mesh_nodes-procedural_generation/

These are starting points, not mandates. In particular, do not introduce geometry clipmaps for a fixed, precomputed flythrough unless research shows they are useful.

Document the selected approach, alternatives considered, known risks, and why the chosen approach is appropriate for the existing terrain and renderer.

### 3.3 Establish a baseline

Before implementation:

- Capture representative frames from the current experiment, including close terrain, steep walls, distant cliffs, river edges, the horizon, and difficult silhouettes.
- Record the current output dimensions, camera path, terrain bounds, generation parameters, material settings, and available performance measurements.
- Determine whether the existing 46-second flythrough can be reproduced deterministically.
- Record the source terrain's geometric detail and any available topology or scalar-field data.
- Identify which parts of the reference are geometry, shading, post-processing, or rendering artifacts.

Create a short implementation plan with explicit milestones. Keep the first milestone focused exclusively on terrain conversion.

## 4. Milestone A — Engine-native terrain conversion

This is the most important part of the project.

### 4.1 Conversion strategy

Choose the least destructive approach that preserves the source canyon.

If the existing generator already produces usable surface geometry, reuse that geometry or adapt it to the engine's mesh representation.

If the source is a heightfield, triangulate it using an appropriate grid or adaptive triangulation while preserving the existing height samples and canyon silhouette.

If the source is an implicit or volumetric representation, investigate extracting an explicit triangle mesh from the existing field. Select an extraction technique based on surface quality, topology, complexity, and the available source data.

If the source contains overhangs or multiple surface layers, do not force it into a single-valued heightfield.

If the source is a raymarched scene with no explicit terrain samples, investigate sampling its existing surface or underlying field. Establish a practical mesh-resolution and approximation-error strategy before committing to a conversion.

Do not simply create a coarse grid, regenerate the terrain with a different noise function, or replace the canyon with a generic game-engine landscape.

### 4.2 Preserve the existing world

The converted scene must retain:

- The original canyon's major silhouette and spatial composition.
- The recognizable river corridor and the relationship between the water and surrounding banks.
- Major cliff faces, rock shelves, spires, and distant terrain.
- The original warm sandstone and cool shadow palette.
- The open sky and visible distance between canyon formations.
- The existing camera trajectory and overall sense of scale.

The original camera path should be reused unless a documented technical issue makes a minimal correction necessary.

If the current terrain includes a river rendered independently of the terrain, preserve that separation. Do not accidentally merge water geometry into the rock mesh or introduce new gaps along the banks.

### 4.3 Mesh construction requirements

The final terrain mesh must have:

- Valid vertex and index buffers using the engine's established mesh conventions.
- Correct triangle winding and consistent front-face orientation.
- Valid, normalized vertex normals or an appropriate existing normal-generation strategy.
- Correct bounds for scene culling.
- Stable world-space coordinates and transforms.
- Appropriate handling of mesh boundaries.
- No unintended holes, cracks, overlapping surfaces, or detached fragments.
- No degenerate triangles that cause rendering artifacts.
- No visible z-fighting between coincident terrain surfaces.
- Sufficient geometric density to retain the existing silhouettes at the closest camera distances.

Preserve important ridge lines and canyon-wall transitions. If the source is sampled into a mesh, use adaptive density or curvature-aware sampling where necessary rather than spending equal geometric effort everywhere.

For large or complex meshes, investigate index-buffer reuse, vertex sharing, mesh partitioning, and appropriate index formats. Partitioning should serve real culling, memory, or draw-call needs, not create unnecessary complexity.

Do not add runtime terrain deformation unless the existing engine architecture requires it. Static geometry is sufficient for this phase.

### 4.4 Surface materials and coordinate mapping

Preserve the original material appearance as closely as the engine permits.

Use existing material functionality first. If the source relies on procedural world-space shading, reproduce that shading using the closest native equivalent.

For steep, irregular terrain, investigate world-space triplanar mapping or another projection strategy that avoids excessive UV stretching. Do not introduce a generic tiled rock texture if the existing canyon's visual quality comes from procedural color, strata, and large-scale surface variation.

Maintain:

- Distinct warm and cool regions.
- Large-scale color variation.
- Readable rock strata.
- Natural-looking light and shadow transitions.
- Surface detail that remains subordinate to the canyon's large forms.

Avoid unnecessary texture dependencies. Prefer existing procedural materials and free resources when they materially improve the result.

### 4.5 Mesh conversion validation

Render the original and converted canyon through the same camera path and compare them at representative points.

Check:

- Close-up surfaces.
- Steep canyon walls.
- Thin ridges and rock shelves.
- River banks and reflections.
- Distant silhouettes.
- Camera motion and temporal stability.
- The first and last portions of the flythrough.

The converted version must not exhibit conspicuous geometric distortion, silhouette collapse, gaps, normal seams, or material-coordinate discontinuities.

Do not proceed to substantial art expansion until the conversion is acceptable.

If the source representation cannot be converted at sufficient fidelity within a reasonable budget, document the limitation and propose a concrete alternative rather than silently substituting a different landscape.

## 5. Milestone B — The canyon's hidden mathematics

Implement the primary audio-reactive visual feature: a geometric revelation system that exposes the triangle structure of the actual terrain.

This should be the signature effect of the first version.

### 5.1 Visual objective

Most of the time, the canyon looks like a natural, beautifully lit sandstone environment.

When the music activates the effect, thin luminous lines appear to trace the underlying triangle mesh. Selected areas illuminate in traveling waves. The wireframe fades smoothly back into the original rock.

The effect should suggest that the canyon has a hidden mathematical structure, not that a game developer has placed a debug wireframe over the scene.

### 5.2 Geometry source

Whenever practical, reuse the terrain's actual triangle topology.

Do not create an unrelated regular grid and project it onto the surface if that would cause the lines to drift away from the real geometry.

Investigate the following implementation options:

1. An existing wireframe or barycentric-coordinate rendering facility in the engine.
2. A dedicated material/shader pass that renders triangle edges.
3. A suitable mesh attribute or edge representation that supports reliable edge detection.
4. A separate overlay mesh only if it can remain precisely aligned with the actual terrain.

Choose the simplest approach compatible with the renderer's existing architecture.

### 5.3 Avoid the common wireframe failure modes

A naive triangle wireframe can expose every diagonal, internal tessellation boundary, and density transition, making the canyon look like an engineering viewport.

Avoid that result.

- Keep line widths visually thin and stable.
- Use distance-dependent fading.
- Prevent dense distant geometry from turning into a solid luminous mass.
- Keep the wireframe behind the natural material's visual priority.
- Avoid obvious z-fighting and depth inconsistencies.
- Avoid excessive overdraw or a full-screen post-process when a localized geometry effect will do.
- Ensure mesh partition boundaries do not appear as arbitrary seams.
- Avoid bright white lines by default.
- Keep the effect legible against both sunlit sandstone and dark rock.

Investigate a topology-aware approach that can distinguish meaningful ridge or structural edges from ordinary triangulation diagonals. If reliable edge classification is too expensive, use a restrained, sparse, or selectively masked wireframe rather than pretending every triangle edge has artistic meaning.

### 5.4 Three related visual modes

Implement one coherent system with a few distinct presentation modes, not three unrelated effects.

**Mode A: Contour tracing**

Subtle lines or bands follow the canyon's surface and emphasize large-scale geological structure.

**Mode B: Triangular revelation**

The underlying triangle topology becomes visible, especially during musical accents.

**Mode C: Geometric dissolution**

A localized section briefly reveals or emphasizes its underlying triangular structure, then returns to its original material appearance.

Mode C must not destroy the terrain, alter its silhouette, or require continuous mesh regeneration. It is a shading and presentation effect, not actual terrain destruction.

Expose these as controllable parameters or clearly defined effect states consistent with the scene's architecture.

### 5.5 Visual parameters

Provide appropriate scene-level controls for:

- Overall effect intensity.
- Wireframe visibility.
- Line width or equivalent edge-coverage control.
- Emission intensity.
- Color and palette.
- Distance fade.
- Contour visibility.
- Transient response strength.
- Sustained response strength.
- Revelation duration and decay.
- Optional minimum activation threshold.

Use the engine's existing parameter and modulation facilities. Do not create an entirely new configuration framework.

Defaults should be artistically restrained and useful immediately when the scene is loaded.

## 6. Milestone C — Musical behavior and audio mapping

Audio behavior must be musical, stable, and appropriate to the signal types actually available in AV Gen.

Do not assume the project has separate kick, snare, hi-hat, or stem signals. Inspect the existing analysis system and determine which signals the scene can use. Prefer general-purpose analysis signals and available stems or channels over inferred percussion identities when appropriate.

### 6.1 Continuous controls versus discrete events

Use continuous signals for:

- Broad material intensity.
- Color movement.
- Contour brightness.
- Ambient atmosphere.
- Water shimmer.
- Flock spread or general activity.

Use transient events for:

- Brief geometric revelations.
- Short light waves.
- Glyph activations.
- Flock takeoff or coordinated movement.
- Other sparse, high-impact events.

Do not make the entire environment pulse in lockstep with every transient.

### 6.2 Suggested mappings

These are starting points for artistic tuning, not mandatory hardcoded assumptions.

**Bass energy**

- Drives broad, slow-moving illumination along the canyon walls.
- Encourages the geometric structure to become more visible during sustained bass phrases.
- Can produce restrained pulses along the river corridor.
- Must not cause the terrain to wobble or physically deform.

**Drum or general transient energy**

- Triggers brief localized geometric reveals.
- Sends a short activation wave across selected terrain regions.
- Can illuminate selected glyphs or structural patterns.
- Stronger events should produce more substantial responses than weaker events.

**Midrange and sustained musical energy**

- Drives slow material color shifts and contour movement.
- Modulates the atmosphere and selected surface features.
- Can influence the broad activity level of wildlife.

**High-frequency energy**

- Can influence subtle surface glints, fine geometric details, or the intensity of selected glyphs.
- Must not create constant high-frequency flicker.

**Large musical accents**

- Temporarily expose a greater portion of the canyon's triangular structure.
- Produce a coherent wave or revelation event.
- Allow the scene to return to its natural appearance afterward.

### 6.3 Mapping implementation requirements

Use the current AV Gen signal and parameter routing system.

- Normalize inputs to meaningful ranges.
- Smooth continuous controls using appropriate attack and release times.
- Apply thresholds and hysteresis where a discrete state is needed.
- Apply cooldowns or event-rate limits where necessary.
- Avoid double-triggering from one transient.
- Avoid triggering based on absolute frame number.
- Ensure the effect behaves consistently at different frame rates.
- Keep signal processing and rendering responsibilities separate.
- Make the response tunable without recompiling the engine.

Do not introduce arbitrary beat synchronization or assume a four-on-the-floor rhythm.

Do not make the scene dependent on a particular audio file, tempo, or stem naming convention.

### 6.4 No artificial latency

Follow the existing audio-analysis and scene-update timing model. Do not introduce additional buffering, next-frame event queues, or delayed parameter updates unnecessarily.

If the existing engine introduces unavoidable signal-to-render latency, measure and document it. Do not mask the problem by building a scene-specific workaround.

## 7. Milestone D — Birds in the canyon

Add a small, visually convincing flock of birds that creates environmental scale and motion.

This is the first secondary world element to implement after the terrain and geometric effects work.

### 7.1 Art direction

Birds should be relatively small dark silhouettes against the sky and sunlit canyon walls. They should feel like wildlife moving through a large space, not particles arranged into a decorative spiral.

Avoid oversized birds, exaggerated cartoon movement, and excessively synchronized formations.

The birds must remain subordinate to the canyon's main composition.

### 7.2 Initial implementation

Start with a modest flock, approximately 20–40 birds, and make the count configurable.

Use:

- One or a small number of shared bird meshes.
- Shared materials.
- Lightweight skeletal animation, vertex animation, or another existing efficient animation method.
- A small set of authored or procedural flight paths.
- Deterministic variation in timing, spacing, altitude, and orientation.
- Basic banking and wing movement.
- Appropriate silhouette and scale variation.

Do not create a complex flocking simulation unless the existing engine already has a suitable implementation and it materially improves the result.

Spline-based flight paths with controlled variation are acceptable and may be preferable for a designed flythrough.

### 7.3 Flight behavior

Birds should:

- Enter and exit the camera's field of view naturally.
- Cross the canyon at multiple distances.
- Follow broad arcs that respect the canyon's scale.
- Occasionally regroup or spread apart.
- Avoid visibly intersecting the main rock surfaces.
- Avoid constant looping directly in front of the camera.
- Remain stable and plausible during camera movement.

Use the existing camera path to design the flight paths. Birds do not need a full navigation or collision system if carefully authored paths achieve the desired effect.

### 7.4 Audio response

Music should influence the flock's behavior rather than simply recolor it.

Potential behaviors:

- Sustained energy changes the flock's spread or overall activity.
- A sufficiently strong transient triggers a coordinated takeoff or a change in direction.
- A major musical accent triggers a brief, more dramatic crossing.
- During quieter passages, birds can glide or remain in the distance.

Use event thresholds, cooldowns, and natural animation transitions. Do not force every bird to flap on every beat.

The flock must also look convincing when no audio events are detected.

### 7.5 Performance

- Use shared geometry and materials.
- Prefer instancing if supported and appropriate.
- Avoid per-bird expensive scene queries or unnecessary CPU work.
- Cull birds outside the useful camera region when practical.
- Avoid shadow casting for every bird if it adds significant cost without visible benefit.
- Keep the count adjustable.

The first implementation should prioritize silhouette, animation, and flight paths over feather detail.

## 8. Milestone E — Original geological glyphs

Introduce an original symbolic language that appears to be carved or embedded in selected canyon surfaces.

The effect should suggest that the canyon contains an ancient intelligence or encoded history.

It must not look like a generic sci-fi HUD, floating UI text, or a random assortment of recognizable real-world religious symbols.

### 8.1 Visual design

Create a small curated set of original glyphs using simple geometric forms, lines, arcs, branching shapes, and unusual combinations of these elements.

Prefer approximately 8–16 well-designed symbols over a large random collection.

Symbols should appear on selected rock faces, shelves, and surfaces that are visible from the existing camera path.

Use appropriate surface placement, orientation, scale, depth bias, and material treatment so they appear integrated with the rock.

Possible implementations include:

- Simple meshes placed close to the rock surface.
- Decals, if the engine supports them.
- A small set of existing material or shader techniques.

Choose the most robust approach available in the engine.

### 8.2 Presentation

Most glyphs should remain subtle, partially obscured, or nearly indistinguishable from the rock at rest.

When activated, they may:

- Illuminate along their paths.
- Reveal a secondary inner pattern.
- Emit a restrained colored glow.
- Activate neighboring glyphs in a spatial sequence.
- Send a short wave of illumination along the surrounding terrain.

Do not make every glyph permanently bright.

Do not place them uniformly across the entire environment.

### 8.3 Audio response

Use musical events and sustained energy to trigger curated groups of glyphs.

- A transient can activate one glyph or a small group.
- Sustained energy can gradually illuminate a larger pattern.
- Major accents can trigger a sequence across several visible rock faces.
- Each activation should have a controlled onset, hold, and decay.

Keep the glyph system deterministic enough that it can be artistically evaluated and debugged.

### 8.4 Optional scope

Glyphs should be implemented after the geometric revelation system and birds. If the terrain conversion or wireframe requires more work, defer the glyphs rather than compromising the foundation.

## 9. Milestone F — Optional riverside campsite

If time and performance permit, add a small, distant campsite beside the river.

This is a secondary discovery, not a primary character scene.

### 9.1 Composition

The campsite should be tiny relative to the canyon, with a small fire, a few environmental props, and two or three mysterious humanoid silhouettes.

It should be possible to see the campsite during at least one useful portion of the existing camera path without requiring the camera to stop or radically change direction.

Do not add a large village, detailed settlement, elaborate architecture, or a new cinematic sequence.

### 9.2 Characters

Use simple stylized silhouettes with enough visual distinction to read as mysterious humanoid figures at a distance.

Possible animation states:

- Sitting near the fire.
- Standing.
- Walking a short distance.
- Looking toward the river.
- Briefly looking toward the camera.
- Returning to a resting state.

A small set of authored animations or simple procedural poses is sufficient.

Do not build a general-purpose character AI system.

Do not attempt to make the figures realistically lip-sync or dance to the music.

### 9.3 Campfire and environmental details

The fire should provide a small, warm local light and subtle environmental animation.

Optional details include:

- A few rising embers.
- Gentle smoke, if supported efficiently.
- Minimal reflection or light on nearby surfaces.
- A small number of props that help communicate the campsite's scale.

Avoid excessive particle counts and large orange explosions.

The fire must not overpower the canyon's established lighting.

### 9.4 Audio response

The campsite should have restrained, contextual behavior.

A major musical accent may cause one figure to stand or look up. Sustained energy can gently increase firelight intensity. Other times, the figures should continue their own slow behavior without obvious beat synchronization.

The campsite must remain visually effective even when it does not react to the music.

### 9.5 Scope gate

The campsite is optional. Do not implement it if doing so would delay the terrain conversion, compromise the geometric effects, or prevent the scene from meeting its performance target.

## 10. Artistic direction and palette

Preserve the current canyon's natural warmth and cool shadow structure.

### Core palette

- Warm ochre, sandstone, copper, and muted coral for illuminated rock.
- Deep indigo, violet, and blue-purple for canyon shadows.
- Pale peach, lavender, and blue for the sky.
- Gold and pale cream for selected highlights.
- A restrained secondary digital color, such as electric violet or cool magenta, for the geometric revelations.

The digital effect should complement the existing colors rather than replace the entire palette.

Allow scene-level color tuning, but do not make a rainbow gradient the default answer to every audio signal.

### Contrast and visual hierarchy

The canyon should remain the dominant visual element.

A useful hierarchy is:

1. Terrain silhouette and major lighting.
2. River and atmospheric depth.
3. Audio-reactive geometric revelations.
4. Wildlife and occasional glyph events.
5. Optional campsite and smaller details.

Do not let particles, wireframe, symbols, or bright emission obscure the terrain for extended periods.

### Avoid

- Constant full-screen neon wireframes.
- Generic green Matrix code raining across the landscape.
- Random floating cubes or spinning shapes.
- Excessive bloom that destroys the rock silhouette.
- Uniformly bright glyphs.
- Overly detailed, realistic characters that compete with the landscape.
- Constant camera shake.
- Audio mappings that produce identical behavior for every musical source.
- Additional procedural terrain noise that changes the canyon's recognizable silhouette.
- An unrelated new scene architecture.

The desired result is a natural environment that periodically reveals its hidden mathematical structure.

## 11. Camera and flythrough

Use the existing 46-second reference flythrough as the baseline.

Preserve its pace, composition, and sense of scale. The converted terrain should be evaluated from the same viewpoints as the reference.

Do not redesign the camera path simply to show off the new effects.

Additional camera movement is optional and should only be introduced if the current path demonstrably fails to show a major feature.

The scene should still look good when inspected through the app's normal camera controls, not only during the reference flythrough.

For evaluation, identify several useful moments:

- An opening view that establishes the canyon.
- A view with clear foreground rock and distant cliffs.
- A section with visible river reflections.
- A close pass along a wall suitable for geometric revelation.
- A wide view suitable for a flock crossing.
- A suitable distant location for the optional campsite.

Use these as repeatable checkpoints for art and performance evaluation.

## 12. Renderer integration

Use the existing renderer's supported material and geometry pipeline.

Do not introduce a new rendering backend, a Metal-specific renderer, or a separate scene rendering framework.

Investigate how to integrate the following into the current pipeline:

- Opaque terrain geometry.
- Existing water rendering.
- Terrain wireframe or contour rendering.
- Glyph surfaces or decals.
- Bird geometry and animation.
- Optional campsite geometry and local light.
- Existing fog, bloom, exposure, and tone mapping.

Respect the renderer's current depth, normal, material, and lighting conventions.

If the wireframe requires a separate pass, document its ordering and depth behavior. Avoid unnecessary render-target copies or full-screen passes.

If the engine lacks a suitable feature, make the smallest architectural addition that solves the scene's actual requirement. Document any reusable renderer capability separately from scene-specific artistic behavior.

## 13. Performance and scalability

### Target

Preferred live target: 1920×1080 at 30 fps.

This is a soft requirement, not a reason to destroy the artistic quality of the canyon. Offline rendering remains a valid outcome if the final scene cannot achieve the target on the intended hardware.

### Measure incrementally

Record:

- Baseline terrain frame time.
- Terrain conversion frame time.
- Geometric revelation overhead.
- Bird-system overhead.
- Glyph-system overhead.
- Optional campsite overhead.
- Final scene frame time.

Measure CPU and GPU costs independently where the existing profiler allows.

Do not rely exclusively on average FPS. Record representative frame-time behavior during the most expensive sections of the flythrough.

### Optimization priorities

1. Reuse the static terrain mesh.
2. Avoid rebuilding geometry every frame.
3. Keep wireframe work limited to useful visible terrain.
4. Avoid unnecessary full-screen rendering passes.
5. Reuse bird meshes, materials, and animation resources.
6. Keep wildlife and campsite logic lightweight.
7. Cull or simplify objects that contribute little at their current screen size.
8. Reuse existing scene and rendering facilities rather than duplicating systems.
9. Avoid speculative optimization before profiling.

If the live budget is exceeded, first reduce optional wildlife density, glyph activation complexity, and secondary effects. Preserve the canyon silhouette and the quality of the primary geometric revelation.

Document which quality settings are useful for live and offline modes if the project supports such distinctions.

## 14. Scene controls and debugging

Expose only controls that are useful for art direction, audio mapping, and performance.

At minimum, provide a way to:

- Enable or disable the geometric revelation.
- Adjust its intensity and emission.
- Tune the continuous and transient response.
- Enable or disable birds.
- Adjust bird count or activity.
- Enable or disable glyphs.
- Enable or disable the optional campsite.
- Disable all audio reactions while keeping the scene visible.

Follow AV Gen's existing parameter, UI, and scene-persistence conventions.

If the engine already supports scene-level modulation routing, make these controls accessible through that system rather than hardcoding signal names into every effect.

Add useful diagnostic information for the terrain mesh, including vertex and triangle counts, bounds, and any available generation or conversion statistics.

Do not build a large bespoke debug panel if the existing profiler and scene controls already cover the need.

## 15. Testing and quality assurance

### 15.1 Mesh tests

Where appropriate for the engine's test architecture, test:

- Mesh construction from representative source terrain data.
- Index validity.
- Triangle winding.
- Normal validity and normalization.
- Bounding-box correctness.
- Degenerate triangle handling.
- Deterministic output for deterministic input.
- Mesh partition boundaries if partitioning is introduced.

Avoid tests that simply duplicate implementation details without verifying useful invariants.

### 15.2 Scene tests

Verify:

- The scene loads through the normal AV Gen path.
- All required assets and materials resolve.
- The camera path is valid.
- Terrain bounds and culling are correct.
- The scene works with audio disabled.
- Audio controls produce bounded, stable responses.
- The scene works with different signal levels and source material.
- Repeated playback does not accumulate state or resources.
- Optional scene elements can be disabled independently.

### 15.3 Rendering tests

Use deterministic render captures where possible.

Compare the converted scene with the reference at matching camera times.

Check for:

- Missing terrain.
- Distorted silhouettes.
- Edge artifacts.
- Cracks or gaps.
- Bad normals.
- Material seams.
- Incorrect river placement.
- Z-fighting.
- Unstable wireframe edges.
- Excessive distant line density.
- Unwanted temporal flicker.
- Broken transparency or depth ordering.
- Birds intersecting the terrain.
- Glyphs floating visibly above or buried inside the rock.
- A campsite that is too large or too visually prominent.

### 15.4 Audio tests

Use a range of representative audio material rather than tuning exclusively to one track.

Test:

- Quiet passages.
- Sustained bass.
- Sparse transients.
- Dense percussion.
- Strong accents.
- Continuous high-frequency content.
- Different tempos and musical structures.
- Periods with no useful transient events.

Verify that the scene remains visually interesting and stable when a particular signal is absent.

### 15.5 Performance tests

Measure the reference flythrough at the target resolution.

Compare the baseline, converted terrain, primary effect, and final implemented scope.

Record frame-time results and identify any performance regression caused by the new systems.

### 15.6 Manual art review

Automated tests are necessary but insufficient. Inspect the actual rendered output.

A mesh can pass all structural tests while looking worse than the original canyon. Final acceptance depends on visual quality as well as correctness.

## 16. Documentation and project hygiene

Update the relevant scene documentation and implementation notes.

Document:

- The source terrain representation.
- The selected mesh-conversion approach.
- Any fidelity limitations.
- Material-coordinate and normal-generation decisions.
- The geometric revelation implementation.
- The available audio controls and recommended mappings.
- Wildlife implementation and limitations.
- Performance results.
- Known issues and deferred optional features.

Remove obsolete experiment artifacts only when they are genuinely unused and the cleanup is clearly within scope. Preserve the reference video and any assets required to reproduce or validate the original terrain.

Do not leave duplicate experimental implementations, dead configuration paths, unexplained fallback logic, or unreferenced generated assets introduced by this task.

Follow the repository's normal branch, formatting, build, test, and CI procedures.

## 17. Required implementation phases

Keep these phases explicit and independently reviewable.

### Phase 1 — Research and terrain conversion

Deliver the native triangle-mesh canyon with its existing camera path and visual identity intact.

Do not start the optional scene expansion until this phase passes review.

### Phase 2 — Geometric revelation

Implement and tune the terrain wireframe/contour system. Validate its visual quality, depth behavior, audio response, and performance.

This is the core artistic feature.

### Phase 3 — Wildlife

Implement the bird flock, including authored flight paths, efficient animation, and restrained musical behavior.

### Phase 4 — Glyphs

Implement the original geological glyph language and its audio-driven activation.

### Phase 5 — Optional campsite

Implement the distant campsite and minimal character animation only if the earlier phases are successful and the remaining scope is justified.

### Phase 6 — Final QA and delivery

Perform full scene testing, visual review, performance measurements, documentation updates, and cleanup.

Do not declare completion merely because the scene compiles or a single screenshot looks correct.

## 18. Acceptance criteria

The task is complete when the following are true:

- [ ] The canyon uses actual engine-native triangle geometry.
- [ ] The conversion preserves the original canyon's recognizable visual identity.
- [ ] The original camera path can be used to evaluate the converted scene.
- [ ] The previous edge distortion is eliminated or reduced to documented, visually acceptable limitations.
- [ ] Terrain normals, winding, bounds, and material coordinates behave correctly.
- [ ] The river and terrain remain spatially consistent.
- [ ] The scene looks compelling with all audio effects disabled.
- [ ] The geometric revelation uses the actual terrain topology or a demonstrably aligned representation.
- [ ] The wireframe is an intentional art effect, not an always-on debug visualization.
- [ ] Continuous and transient audio signals produce distinct, stable, tunable behaviors.
- [ ] Audio mappings do not assume a fixed tempo or four-on-the-floor rhythm.
- [ ] Birds use efficient shared resources and convincing flight paths.
- [ ] Glyphs are original, selectively placed, and integrated with the terrain.
- [ ] The campsite is either implemented successfully or explicitly deferred with a reason.
- [ ] The scene has been evaluated through representative portions of the reference flythrough.
- [ ] CPU/GPU performance has been measured at 1080p.
- [ ] The 30 fps live target has been evaluated without sacrificing the quality of the terrain to achieve it.
- [ ] Automated tests and applicable CI checks pass.
- [ ] Documentation and generated assets are consistent with the final implementation.
- [ ] The final report clearly distinguishes completed work, deferred work, performance findings, and remaining limitations.

## 19. Final report

At completion, report:

1. The terrain source representation and the conversion approach selected.
2. What was preserved from the reference and any unavoidable differences.
3. The triangle and vertex counts, mesh memory footprint if measurable, and relevant generation details.
4. How the geometric revelation is implemented.
5. Which audio signals and parameters drive each behavior.
6. Which secondary elements were completed and which were deferred.
7. Representative before/after render captures or a comparison video, if the existing workflow supports them.
8. CPU/GPU frame-time measurements at 1920×1080.
9. Tests and CI checks executed, including any failures or unavailable checks.
10. Documentation and asset changes.
11. Remaining limitations and the recommended next step.

The final priority order is:

**Preserve the canyon → convert it faithfully → make its geometry musically reveal itself → add wildlife → add geological symbols → optionally add the campsite.**

Do not sacrifice the terrain's existing quality in pursuit of additional features. The canyon is the centerpiece; every new system should make that centerpiece more compelling.
