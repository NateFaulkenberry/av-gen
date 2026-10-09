# Freeze the Wide Shots, Finish the Macro Showcase

(The owner's spec, verbatim, 2026-10-09, given after the cinematic finishing pass (ada3a3a8, `finishing/E-final/`). It governs from here. OWNER CORRECTION: "Glowmere Valley" in this spec means BIOVALLEY (this scene, examples/biovalley). Do NOT touch Glowmere Valley (examples/glowmere*, GV3) in any way. Coordinator's notes:
- **Owner stops:** after the Phase 2-3 static macro still (Phase 4 audio needs visual approval), and again at Phase 7.
- **Division of work:** the valley lead does Phases 1-4 (the macro). A second agent does Phase 5 (the bounded river-reflection experiment) and the Phase 6 validation (a clean-build test run of the engine changes), on a separate branch cut from ada3a3a8, and is also on call for Blender asset requests.)

# AV Gen — Glowmere Valley: Freeze the Wide Shots, Finish the Macro Showcase

## Mission

We have completed the current cinematic finishing pass on `proto/bio-valley`, commit `ada3a3a8`.

Review the current output at:

`~/Desktop/av-gen-review/45-bioluminescent-valley/finishing/E-final/E-contact-sheet.png`

Also inspect `riverbank-4k.png`, `vista-4k.png`, and `08-finishing-log.md` in that directory.

The next milestone is not another general-purpose art pass. It is a finished, audio-reactive macro scene that demonstrates AV Gen's artistic potential and can eventually be optimized for real-time performance.

The macro organisms and meadow are now acceptable foundations. The riverbank and vista remain unfinished, but broad iterations are delivering diminishing visual returns.

Keep the current branch unmerged until the changes have been reviewed and the full relevant test suites pass.

## Phase 1 — Preserve the work and establish a baseline

1. Inspect the current branch, commit, working tree, scene files, and rendering configuration.
2. Identify which changes are shared engine improvements and which are specific to Glowmere Valley.
3. Preserve the current cinematic renders, asset versions, and references.
4. Produce a baseline macro render using the actual intended showcase camera.
5. Inspect the macro at full resolution, not just in the contact sheet.
6. Use the existing visual critic to identify the three highest-impact remaining issues.

Do not regenerate the entire scene or replace successful assets without evidence that doing so will improve the result.

Do not spend this phase measuring performance.

## Phase 2 — Finish the macro composition

Concentrate on the existing organisms, moss, roots, ground litter, lighting, and composition.

Evaluate:

* Whether the main organisms have a clear focal hierarchy.
* Whether the center of the frame is too evenly bright.
* Whether supporting organisms compete with the heroes.
* Whether the ground has convincing depth and transitions.
* Whether the organisms feel integrated into the substrate.
* Whether the materials remain distinctive in the final lighting.
* Whether the composition has sufficient dark negative space.
* Whether the camera and depth separation make the scene feel cinematic.

Prefer a few deliberate corrections over additional asset proliferation.

Use Blender MCP if an authored asset or material demonstrably improves the result, but do not introduce new geometry merely to make the scene busier.

Render after each meaningful change. Keep the best version as the new baseline.

## Phase 3 — Make the macro showcase-ready

After approving the static composition, investigate the final presentation.

Test appropriate combinations of:

* Camera framing and motion.
* Depth of field, if supported.
* Selective bloom.
* Local colored illumination.
* Reflected cyan, blue, and emerald light.
* Atmospheric depth.
* Subtle foreground and background separation.
* Controlled exposure and tone mapping.

Keep the luminous organisms legible. Do not use excessive bloom to hide weak geometry or merge separate subjects.

Create a final still and a short camera sequence. Evaluate both through the existing visual critic.

The result should feel like a carefully authored cinematic environment, not a procedural asset demonstration.

## Phase 4 — Add audio reactivity

Only begin after the static scene is visually approved.

Use AV Gen's existing audio-analysis, signal-bus, modulation, and timeline systems. Inspect the current implementation before adding new infrastructure.

Design a restrained hierarchy of musical responses:

* Fast transients: brief, localized pulses in selected luminous structures.
* Medium timescales: gentle intensity and color changes in supporting organisms.
* Slow musical evolution: broader changes in environmental illumination and atmosphere.
* Phrase-level events: occasional coordinated responses that feel intentional.

Do not synchronize every object to the same envelope. Avoid uniform flashing, random motion, and constant full-scene color cycling.

The scene must remain beautiful when the audio input is silent or unchanged.

Produce a short sequence that demonstrates the range of response while preserving the composition.

## Phase 5 — One bounded river-reflection experiment

This is a separate, optional investigation. It must not block the macro showcase.

The latest cinematic water implementation improved the reflections' color and environmental integration, but the visible cyan and pink reflection paths now appear as stacks of rectangular blocks.

Inspect the actual implementation and determine the cause before modifying it.

Check whether the artifact originates from sprite geometry, reflection-buffer resolution, screen-space limitations, sampling, compositing, or another part of the pipeline.

Render a controlled comparison between:

1. The current water implementation.
2. A version that addresses the identified cause and produces smoother, more continuous reflection paths.
3. A planar-reflection or other higher-quality alternative only if it is appropriate for the current camera and geometry.

Preserve the current scene, lighting, and framing during the comparison.

Do not add more random glints or increase bloom as a substitute for correcting the reflection structure.

Use the critic to judge whether the result is genuinely better. If the improvement is not clear, document the remaining issue and defer further river work.

Do not start another broad vista or meadow art pass.

## Phase 6 — Validate and integrate the engine work

Before merging, inspect the engine changes introduced during the cinematic pass, including the water path, HDR color handling, cloud renderer, material-type capacity, and failed-scene-load handling.

Run the full relevant test suites from a clean build configuration. Include existing regression tests and any new tests needed for the engine changes.

Verify that a failed scene load returns a real failure and cannot silently render the default scene while reporting success.

Confirm that materials beyond the previous eight-type limit are handled correctly and that the moon's material no longer disappears silently.

Record the exact commands run and their results. Do not report tests as passing unless they were actually completed.

Keep scene-specific work and shared engine changes distinguishable so they can be reviewed independently.

## Phase 7 — Deliverables

Provide:

1. An approved high-resolution macro still.
2. A short audio-reactive macro sequence.
3. A visual-critic report comparing the baseline and final macro.
4. A short summary of the changes retained and rejected.
5. The result of the bounded river-reflection experiment, if completed.
6. A clean test report for the engine changes.
7. A recommendation for the next stage of macro performance profiling.

Do not merge until the relevant tests pass and the changes have been reviewed.

## Success criterion

The next milestone succeeds when we have one compelling, coherent, musically expressive macro environment that can serve as an AV Gen showcase.

Do not measure progress by the number of shaders, assets, or engine features added.

Measure it by the quality of the final render, the intentionality of its motion, and whether the result is worth showing to an audience.
