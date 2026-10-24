# Aurora Borealis: owner's brief (verbatim, 2026-10-24)

Coordinator notes (facts checked 2026-10-24):
- The stems are `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav` (not `audio/feline-footwear-stems`); the mix is `assets/audio/feline-footwear.wav`.
- Since ADR-1620 (option B) a mix has no kick; kick/snare/hat come only from a source with role `drums` (ADR-1630: `role.drums.kick/snare/hat`, `stem.<key>.*`). The mix keeps snare and hat.
- The Living Prism engine work kept on main: ADR-1600 (honest profiler), 1601 (envelope), 1602 (`signal.*`), 1603 (refraction), 1604 (onset register), 1605 (sound-to-light), 1606/1612 (MIDI pickup, control rests), 1607/1613 (upscaler), 1608 (live perf baseline), 1609/1610 (latency levers), 1620, 1630-1632 (stem roles, history from a source, stem-set check), 1640 (dispersion), 1641 (aurora ribbons), 1642 (terrain textures), 1643 (water flow streaks).
- Live target is 1080p30 (owner's standing rule); the live ladder accepts Medium or better >= 90% of frames (Living Prism 03 s14).

---

Let’s start work on a new live scene using stems for testing now and live channel support later

Task: Build Aurora Borealis — Stem-Driven Live VFX Scene

You are working in the av-gen C++23 audiovisual engine. Build a new production-quality live scene called Aurora Borealis, inspired by the visual direction of Digital Alpine’s Glass Caldera, but with a distinct art direction centered on a luminous aurora, faceted mountain ranges, and a reflective river valley.

This is a scene-production task, not an engine rewrite or a research-only exercise. The final result must be visually compelling, musically expressive, stem-driven, and capable of sustaining the engine’s 30 FPS live-performance target.

The existing Living Prism work may have introduced useful architectural improvements. Preserve and reuse valuable engine changes, but do not reproduce Living Prism’s visual complexity or rendering cost. We are scrapping that scene, not necessarily its underlying improvements.

Continue using the existing Feline Footwear stem set for development and validation.

1. Mandatory workflow

Do not begin by immediately implementing geometry or shaders. First establish how the engine and its existing scenes actually work.

Use two parallel subagents for independent, focused research. Give each agent a bounded investigation, then synthesize their findings into one implementation plan before modifying production code.

Subagent A: Scene architecture and visual rendering

Investigate:

* The existing Digital Alpine scene, especially Glass Caldera and its implementation.
* How scenes are registered, configured, instantiated, rendered, and exposed through the UI.
* Existing procedural terrain, faceted mesh, water, reflection, atmospheric, sky, aurora-like, emissive, and post-processing capabilities.
* Existing material and shader systems and the cost of their relevant render paths.
* Existing camera movement, scene animation, scene configuration, and scene-specific controls.
* Any useful assets, generators, utilities, and examples that can be reused.

Identify the simplest existing rendering approach that can deliver the intended visual quality within the live frame budget.

Do not assume an existing capability is inexpensive merely because it exists. Establish its actual cost and limitations where possible.

Subagent B: Stem analysis, musical responsiveness, and performance

Investigate:

* The current Feline Footwear stem assets in audio/feline-footwear-stems.
* How the drums, bass, and synth stems are loaded, synchronized, analyzed, and routed through the engine.
* Existing stem-specific analysis sources and their actual timing relative to playback.
* The current signal-bus, modulation, parameter, and scene-reactivity architecture.
* Relevant Living Prism changes, especially same-frame signal propagation and the kick/snare/hi-hat detector phase-out work.
* Existing profiling, GPU timing, CPU timing, frame pacing, and live-mode performance tools.
* Any known bottlenecks or latency issues that would affect this scene.

Establish the correct way to build stem-driven visual behavior using the current engine. Identify which signals are already available and which, if any, are genuinely missing.

Do not create a parallel audio-analysis pipeline simply to make the scene work.

Synthesis and implementation plan

After both investigations, produce a concise, actionable plan covering:

1. The scene architecture and registration path.
2. The visual composition and rendering techniques.
3. The mapping of each stem to specific visual behaviors.
4. The use of existing signals and modulation facilities.
5. The performance budget and validation procedure.
6. Any necessary engine changes, with justification.
7. The implementation order and acceptance criteria.

Then proceed with implementation. Do not stop after writing the plan unless a genuine blocker requires a decision from me.

If a third-party or repository research tool is available, use it selectively to resolve specific technical uncertainties. Do not let research become a substitute for building and measuring the scene.

2. Art direction

Core concept

Aurora Borealis is a stylized, atmospheric mountain landscape viewed from a slowly moving camera traveling through a valley at dusk.

A wide, mirror-like river winds through the center of the valley. Angular, faceted mountain ranges rise along both sides and recede into the distance. The upper portion of the frame is dominated by an enormous open sky filled with layered, flowing aurora curtains.

The aurora is the primary visual feature. It paints the surrounding landscape with shifting, luminous color, and its changing forms and colors are musically expressive.

The scene should feel like a sophisticated digital painting brought to life in a real-time 3D environment. Favor deliberate composition, elegant silhouettes, rich color relationships, and handcrafted-looking forms over generic realism.

The target is a polished audiovisual performance environment, not a terrain demo, an abstract shader test, or a photorealistic nature simulator.

Composition

Build a clear, layered composition with the following elements.

Foreground

* A limited amount of dark, faceted terrain and riverbank geometry.
* Enough visible detail to establish scale and movement without cluttering the frame.
* Subtle reflections and colored light where the aurora reaches the foreground.
* Avoid foreground objects that obstruct the aurora or make the camera path feel cramped.

Midground

* The principal valley and winding river.
* Distinct mountain forms with large, readable planar facets.
* A few carefully chosen ridges, slopes, and silhouettes that direct the eye toward the distant valley.
* Terrain with intentional variation in height, shape, and silhouette. Do not generate a field of identical triangular peaks.

Background

* Multiple layers of progressively smaller, less contrasty mountain ranges.
* Atmospheric depth created through color, luminance, and restrained fog or haze.
* A distant horizon that establishes a convincing sense of scale.
* No unnecessary buildings, architecture, elaborate vegetation systems, or decorative objects.

Sky

* A large, uninterrupted sky occupying a substantial portion of the image.
* A restrained twilight gradient, moving from deep indigo and violet toward lighter colors near the horizon.
* Optional subtle stars, provided they do not compete with the aurora.
* Broad, layered aurora ribbons and curtains with distinct silhouettes, folds, and internal color variation.

The aurora should occupy approximately the upper half of the composition, with enough flexibility for its shapes to move and expand without becoming cropped or obscured by mountain silhouettes.

These proportions are a starting point, not rigid geometric constraints. Use the actual camera framing to achieve a compelling composition.

Mountain geometry and materials

Use faceted, low-complexity terrain with an intentional art direction.

* Build a handful of distinctive hero peaks and connected mountain ridges.
* Use large planar faces with variation in slope, scale, and orientation.
* Establish depth using silhouette, atmospheric perspective, and layered color.
* Use a controlled material palette of deep blue, indigo, violet, muted lavender, and selected aurora-tinted highlights.
* Allow the aurora to influence the visible colors of mountain faces.
* Keep the silhouette readable against the sky.

Do not rely on random triangulation alone to create an attractive landscape. Procedural generation, if used, must have deliberate controls over peak placement, ridge shape, valley width, and silhouette.

Avoid excessive geometric detail, tiny decorative peaks, generic low-poly terrain, and noisy per-triangle color variation.

The result should look art-directed rather than randomly generated.

The river

The river is a major compositional element, not an incidental water plane.

* It should wind through the valley and lead the eye toward the horizon.
* Its width and curvature should be carefully chosen for the camera’s perspective.
* The surface should reflect the aurora and mountain silhouettes.
* Use restrained animated ripples or normal distortion to break up the reflection.
* Let brighter aurora regions produce corresponding bands of color on the water.
* Keep the water relatively calm, with no large waves, splashes, or complex fluid simulation.

Prioritize a convincing reflection and color response over physically complete water rendering.

Use the existing reflection implementation if it meets the quality and performance requirements. Otherwise, evaluate a simpler reflection approximation before considering expensive alternatives.

The scene must remain attractive when the aurora is dim. The river should still have a readable surface, depth, and relationship to the mountains.

Aurora construction

This is the scene’s hero effect. Invest the majority of the scene’s visual development effort here.

The aurora should consist of a small number of broad, layered, flowing light curtains rather than a huge collection of independently animated elements.

Develop:

* Distinct large-scale ribbon shapes.
* Smooth, flowing silhouettes with broad folds and bends.
* Layered translucency or an equivalent technique that creates a luminous sense of depth.
* Internal gradients that transition through several related colors.
* Fine internal striations or edge detail, used selectively.
* Brighter folds and luminous edges that help define the shape.
* Subtle animated motion across the curtains.
* A way for the ribbons to brighten, spread, contract, or change their shape in response to music.
* A convincing relationship between the aurora’s shape, its emitted color, the surrounding mountain faces, and the river reflection.

Do not simply place animated sine waves across a flat sky, stretch a noise texture over a plane, or use a generic particle system as the entire effect.

Procedural techniques are welcome when they produce art-directed results and remain inexpensive.

Explore the smallest set of surfaces, shader operations, and lighting techniques that achieves the intended appearance. Prefer existing renderer features and established engine patterns.

True volumetric rendering, expensive ray marching, complex refraction, and high-quality global illumination are not prerequisites. Do not introduce any of them unless measurement demonstrates that the desired visual quality cannot be achieved more efficiently.

3. Live rendering is a hard constraint

The scene must be designed for the engine’s live-performance use case.

Target: sustained 30 FPS on the established M2 Max / Apple Silicon Metal-Dawn development baseline, at the intended 1080p live resolution.

A 30 FPS frame budget is approximately 33.3 ms for the complete frame. That is the budget for the entire engine, not a budget that the scene can consume by itself.

The renderer already has other workloads, so aim to leave meaningful headroom. Do not treat a barely passing average as sufficient.

Performance requirements

* Profile the scene in the actual live rendering path.
* Measure CPU and GPU frame times separately where the tooling supports it.
* Record the baseline before scene-specific implementation.
* Track performance as the landscape, aurora, water, and musical behaviors are added.
* Measure representative high-cost moments, including the brightest and most animated aurora states.
* Watch for spikes, frame pacing problems, unnecessary allocations, and work that grows with time.
* Keep geometry, draw calls, overdraw, transparency, and shader complexity under control.
* Avoid CPU-side per-frame geometry regeneration and unnecessary per-frame asset processing.
* Avoid introducing extra full-screen passes without a demonstrated visual benefit.
* Reuse cached or persistent resources where appropriate.
* Prefer stable, predictable costs over a scene that is fast on average but intermittently misses frames.

If the engine supports dynamic quality settings, use them only when they are appropriate to the existing live architecture. Do not hide an expensive design behind automatic quality reduction and call the scene complete.

Performance validation

Test at the intended resolution with representative Feline Footwear playback.

Record:

* CPU frame time.
* GPU frame time.
* Sustained FPS.
* Worst observed frame-time spikes.
* Rendering settings and resolution.
* The scene state and musical passage used for each measurement.

Use existing profiler and capture tools rather than introducing a new benchmarking framework.

If the scene misses the target, identify and fix the actual bottleneck before adding further polish. The acceptance target is sustained 30 FPS with reasonable headroom, not simply a successful launch or a single frame-rate reading.

Do not claim that the target has been met without measured evidence. If hardware or tooling limitations prevent a definitive result, state exactly what was tested and what remains unverified.

4. Feline Footwear stem-driven musical responsiveness

Continue using the existing three-stem Feline Footwear development set located in audio/feline-footwear-stems.

The current stems are:

* Drums
* Bass
* Synth

Use the actual existing assets and the engine’s current stem-aware playback and analysis architecture. Inspect the repository to establish exact filenames, duration, synchronization, and available analysis signals before implementing mappings.

Do not replace these assets with a different track, use a generic synthesized signal in place of the stems, or fall back to mixed-track analysis when the stem signals are available.

Playback must remain synchronized and behave as it does in the established live scene workflow. The stems are analysis sources for visual control; do not build a new multichannel output path or change the intended playback mix.

Musical principle: context determines color, events determine accents

The scene should have two distinct layers of musical behavior.

Musical context determines the overall visual state: dominant palette, secondary palette, brightness, contrast, and broad movement intensity.

Stem-specific events produce local visual responses: waves through the aurora, brief bright folds, color accents, and corresponding reflection changes.

The scene should respond to the musical arrangement as it evolves, not merely flash on every onset.

Drums: transient accents and traveling waves

The drum stem should primarily drive short, clearly timed visual events.

Use existing drum analysis signals where available, including kick, snare, and hi-hat signals if the current implementation provides them.

Possible mappings:

* Kick: a broad wave of luminance or color that propagates through the lower aurora and into the river reflection.
* Snare: a more localized aurora fold, brief edge brightening, or a sharper ripple of reflected light.
* Hi-hat: restrained fine-scale shimmer or subtle movement in selected aurora details.

These mappings are starting points. Choose the behaviors that look best in practice and remain distinct without producing visual clutter.

Do not create three unrelated, constantly firing animations simply because three drum signals exist.

Drum responses must have clear attacks, controlled durations, and appropriate decay. A kick should not leave the entire scene flashing for several seconds, and a hi-hat should not make the aurora look noisy.

First verify that the current stem-specific detectors fire correctly on the actual drum stem. The existing mix-detector phase-out work and its known timing concerns must be taken into account.

Bass: broad movement and low-frequency energy

Use the bass stem to influence large-scale visual motion and the energy of the lower aurora curtains.

Potential mappings:

* Broad waves traveling across the aurora.
* The amplitude or depth of large ribbon folds.
* Gradual brightness changes in the lower curtains.
* The intensity of aurora color reflected in the river.
* Slow changes in the scale of the flowing forms.

Favor smooth, continuous movement rather than making every bass note create a discrete flash.

The bass should make the environment feel physically connected to the music without making the mountains visibly deform or requiring expensive geometry updates.

Synth: palette, atmosphere, and sustained motion

The synth stem should contribute strongly to the aurora’s character and evolving color.

Potential mappings:

* Gradual changes between complementary palette states.
* Secondary color intensity and the balance between warm and cool hues.
* The width, persistence, and flow of selected aurora ribbons.
* Slow changes in atmospheric luminance.
* Selective brightness in the river reflection and mountain facets.

Use suitable existing amplitude, spectral, or other musical-context signals if available. Do not assume that a signal can distinguish harmonic notes or chords unless the analysis actually supports that interpretation.

The synth should influence the atmosphere without taking full control of every visual parameter.

Combined musical context

Combine the three stems into a coherent scene state.

For example, a sustained synth passage may establish a violet-dominant sky, the bass may add a broad flowing wave, and a kick may briefly send a bright accent across that wave and into the river.

The important behavior is that these elements interact as parts of one visual composition.

Implement a restrained, curated set of palette states and smoothly interpolate between them. Use the available musical context to select or blend those states. Avoid arbitrary per-frame random colors.

The scene should not be forced into a conventional green-only aurora. Violet, indigo, electric blue, lavender, magenta, and pale luminous pink are central to the intended visual identity. Other colors may be used selectively if they improve the composition.

Keep palette changes coherent across the aurora, water, and terrain.

Timing and signal correctness

Correct timing is essential for a live audiovisual instrument.

* Use the existing synchronized stem playback and analysis path.
* Preserve the best available same-frame signal propagation.
* Avoid introducing unnecessary envelope or macro hops that add a frame of latency.
* Avoid quantizing all visual events to a fixed update cadence when the engine supports more direct timing.
* Ensure that transient signals have useful attack and release behavior.
* Ensure sustained controls remain stable between analysis updates.
* Avoid having several smoothing stages unintentionally erase or delay musical accents.
* Test for missed peaks, stuck values, excessive triggering, and timing drift.

Do not bypass the engine’s architecture with a scene-specific collection of ad hoc audio callbacks. Use the established signal and parameter systems, and make targeted architectural improvements only when an actual deficiency prevents the scene from working correctly.

5. Scene architecture and implementation boundaries

Create Aurora Borealis as a proper, independently selectable scene using the existing scene registration and configuration conventions.

Follow the existing patterns for:

* Scene lifecycle and initialization.
* Asset loading and resource ownership.
* Procedural mesh generation.
* Material and shader setup.
* Camera movement and scene animation.
* Signal and parameter registration.
* Scene controls and serialization, where applicable.
* Cleanup and repeatable scene switching.

Use existing generators, materials, reflection features, modulation sources, and profiling facilities wherever practical.

Keep scene-specific code focused and understandable. Avoid introducing a new generalized framework for a single scene.

Do not add dependencies unless absolutely necessary.

Do not rewrite the renderer or introduce an alternative rendering backend.

Do not create backwards-compatibility shims for abandoned scene experiments. If an engine improvement is useful, implement it cleanly using current conventions.

Preserve useful Living Prism engine changes and avoid unnecessarily reverting or duplicating them. The scene should benefit from prior work without inheriting Living Prism’s expensive visual design.

If an engine-level fix is required, document the concrete problem, the minimal change, and why a scene-level solution is insufficient.

6. Visual development and iteration

Build the scene in complete visual layers, keeping it runnable throughout development.

Milestone A: Complete static composition

Implement the valley, faceted mountain ranges, river, sky, camera, and basic lighting.

The scene should already look like a deliberate digital painting. Use placeholder materials only during initial blockout.

Validate the composition from the actual live camera, not solely from editor or free-camera views.

Milestone B: Hero aurora

Add the aurora as a standalone visual effect with its own animation.

At this stage, focus on the quality of the silhouettes, folds, gradients, layering, and luminous appearance.

The aurora must look compelling without audio reactivity. Do not proceed on the assumption that animation alone will make an underdeveloped effect visually convincing.

Milestone C: River reflection and light integration

Make the aurora feel connected to the landscape.

Ensure that reflected color and the surrounding lighting respond coherently to the aurora’s changing visual state.

Prioritize the most visible and effective lighting interactions. Avoid a costly lighting solution that has little impact on the final camera image.

Milestone D: Stem integration

Connect the three stems to the selected visual parameters.

Start with the overall palette and broad motion, then add distinct transient responses.

Validate each stem independently before enabling the combined behavior. Provide a way to inspect or temporarily isolate stem contributions using existing debugging or scene-control patterns.

Do not leave development-only visual debug overlays permanently enabled in the finished scene.

Milestone E: Live optimization and polish

Profile the full scene while Feline Footwear plays.

Improve the most significant visual and performance deficiencies. Polish the aurora’s movement, reflection behavior, color transitions, and the landscape’s visual hierarchy.

If an effect is too expensive, simplify its implementation rather than accepting a missed frame-rate target.

7. Acceptance criteria

The task is complete when all of the following have been addressed.

Scene and visual quality

* [ ]	Aurora Borealis is registered and selectable as a proper scene.
* [ ]	The landscape has a clear mountain-valley composition with a winding mirror river.
* [ ]	The mountain silhouettes and facets look intentional and visually distinct.
* [ ]	The sky has sufficient negative space for the aurora to dominate.
* [ ]	The aurora has convincing layered, flowing shapes and a coherent luminous appearance.
* [ ]	The river and terrain respond visually to aurora color changes.
* [ ]	The scene looks complete and attractive even when musical input is quiet.
* [ ]	The scene avoids unnecessary geometry, decorative clutter, generic procedural noise, and expensive effects that do not materially improve the image.

Musical responsiveness

* [ ]	The scene uses the actual Feline Footwear drums, bass, and synth stems.
* [ ]	The stem assets remain synchronized with playback.
* [ ]	The stems contribute distinct and understandable visual behaviors.
* [ ]	Musical context produces coherent, smoothly evolving palette changes.
* [ ]	Drum events produce timely, restrained accents.
* [ ]	Bass influences broad movement and low-frequency visual energy.
* [ ]	Synth influences sustained atmosphere, palette, and aurora character.
* [ ]	The combined behavior feels composed rather than like independent visualizers layered together.
* [ ]	Signal timing, transient detection, and smoothing have been verified with actual playback.

Performance and reliability

* [ ]	The scene is tested in the real live rendering path.
* [ ]	Performance is measured at the intended 1080p resolution on the established M2 Max baseline, or any unavailable test is explicitly identified.
* [ ]	Sustained 30 FPS is demonstrated with the actual stem set and representative high-cost visual states.
* [ ]	CPU and GPU frame-time measurements are recorded where available.
* [ ]	Performance regressions are addressed rather than hidden by averages or quality reductions.
* [ ]	Repeated scene initialization, playback, and teardown do not introduce obvious resource leaks or accumulating work.
* [ ]	Relevant automated tests and repository checks pass.

Engineering quality

* [ ]	Existing engine features are reused appropriately.
* [ ]	No unnecessary renderer rewrite or parallel audio pipeline is introduced.
* [ ]	Any required engine-level modifications are narrowly scoped and documented.
* [ ]	Documentation and scene configuration reflect the actual implementation.
* [ ]	The final changes are reviewed for obsolete code, debugging artifacts, unnecessary complexity, and unintended regressions.

8. Final report

When finished, provide a concise engineering report covering:

1. The final visual design and how the scene is constructed.
2. The stem-to-visual mapping, including which signals are used.
3. The timing behavior and any known latency limitations.
4. The important rendering techniques and why they were chosen.
5. Measured CPU/GPU performance, resolution, sustained FPS, and observed frame-time spikes.
6. The relevant tests and validation results.
7. Any engine-level changes made and why they were necessary.
8. Any outstanding visual, musical, or performance deficiencies.

Include representative screenshots or short clips of the finished scene if the existing tooling supports capturing them.

Be explicit about anything that remains incomplete or unverified. Do not report a passing performance target, working stem mapping, or successful test that has not actually been demonstrated.

Guiding principle

Build a beautiful, stem-driven aurora over a simple reflective mountain valley, and make it reliably perform at 30 FPS live.

The visual sophistication should come from art direction, layered light, carefully designed color transitions, and musical responsiveness — not from making the scene or renderer unnecessarily complicated.
