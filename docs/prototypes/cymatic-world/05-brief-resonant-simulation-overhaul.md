# AV Gen — Cymatic World: Resonant World Simulation & Dynamic Geometry Overhaul

(Owner brief, received 2026-10-16, saved verbatim. Builds on the colour pass at 129a137a on proto/cymatic-split.)

## Mission

Transform the existing Cymatic World scene from a largely static, procedurally sculpted terrain with audio-reactive color and modest vertical displacement into a genuinely dynamic, continuously evolving cymatic environment.

**The entire visible world must behave like a resonating surface.** Its geometry should continuously reorganize into intricate patterns of peaks, troughs, ridges, valleys, nodal boundaries, lobes, and interference structures. The motion must be driven by music, and the world must feel as though sound is exciting a resonant physical medium.

The current scene has a promising visual identity. Preserve its deep navy surfaces, cyan and teal grazing highlights, fuchsia resonance interiors, dramatic dark hollows, and close, ground-filling composition. The screenshot shows a direction worth developing, not a design that needs to be discarded.

The primary deficiency is behavioral: the current shape looks almost frozen, with audio mostly changing its color and slightly moving its geometry. We need to replace that behavior with a robust resonant deformation system.

This is a **simulation, geometry, and animation overhaul**, followed by visual refinement. Do not treat it as another palette pass.

## 1. First, investigate the current implementation

Before changing code:

1. Locate the Cymatic World scene, its procedural geometry generation, its existing audio-analysis bindings, and its rendering/material code.
2. Determine exactly how the current terrain and cell structures are generated.
3. Identify which geometry is static, which vertices or parameters are animated, and which visual features are purely shader-driven.
4. Find the implementation of the existing Cymatic Plate scene. Study how it achieves its particularly compelling audio synchronization, and reuse its proven approaches where appropriate.
5. Inspect AV Gen's current GPU capabilities and scene-update architecture. Determine whether dynamic vertex displacement, procedural GPU evaluation, compute-based simulation, or a combination fits the existing renderer best.
6. Establish a baseline with screenshots, a short capture covering quiet and loud passages, and GPU/CPU timings.

Do not assume that the current geometry representation is suitable for the new motion. Establish whether it should be replaced, subdivided, procedurally generated at higher resolution, or displaced in a shader.

Do not blindly copy Cymatic Plate's implementation. The goal is to achieve a similar level of musical responsiveness in a continuous world-scale environment.

## 2. The central concept: a resonant field, not a collection of animated craters

The current radial hollows and ridges may be retained as the initial visual character, but they must no longer define an essentially immutable topology.

Build a time-dependent resonant field that controls the surface's actual shape.

Conceptually, the surface height at position (x, y) and time t is:

    H(x, y, t) = base_shape(x, y)
               + resonant_modes(x, y, t)
               + interference(x, y, t)
               + transient_response(x, y, t)

This is a conceptual model, not a requirement to implement these exact function names or a specific mathematical solver.

The critical requirement is that the components interact to create coherent, spatially varying deformation. Do not implement four independent layers of generic noise and call it resonance.

The field must determine the position of the surface itself. Color, emission, highlights, and secondary effects should derive from the same field so that the geometry and appearance remain visually synchronized.

### Required visual behavior

During playback, the viewer should clearly see:

- Large regions of the terrain rise and sink relative to their surroundings.
- Ridges migrate, bend, split, reconnect, and disappear.
- Basins deepen, flatten, stretch, and deform.
- Closed loops become elongated, break apart, and reform.
- Nodal boundaries shift as the active resonant modes change.
- Several overlapping spatial patterns create interference, producing complex, asymmetric structures.
- Broad, slow deformations coexist with fine, rapid oscillations.
- Musical transients cause localized physical disturbances rather than merely brighter pixels.
- Sustained musical energy excites the field for as long as the sound persists, with a natural decay afterward.

The result should look as though a vast, strange, resonant material is undergoing complex vibration.

It must NOT look like a static terrain with a sine wave applied to every vertex, a noise-displaced landscape, a pulsing radial tunnel, or a collection of craters bobbing up and down.

## 3. Build a coherent resonant-mode system

Implement a small, controllable collection of spatial modes whose weighted superposition defines the primary motion.

A useful starting point is approximately 6–16 active modes, adjusted to the scene's performance budget and visual quality. This is a starting range, not a hard architectural constraint.

Each mode should have:

- A spatial pattern.
- A characteristic spatial scale.
- A phase.
- An amplitude or excitation envelope.
- A frequency or temporal response.
- A contribution to the total displacement.
- A contribution to the derived resonance field used by materials and effects.

### Mode families

Develop multiple families of patterns that can coexist:

**A. Broad, low-order modes**

Large lobes, sweeping ridges, expansive depressions, and broad alternating regions of positive and negative displacement.

These establish the movement visible across the full frame. They should be particularly responsive to bass energy and sustained low-frequency musical content.

**B. Radial and ring modes**

Concentric and partially concentric patterns, expanding and contracting lobes, curved nodal boundaries, and ring-like structures.

Retain the radial visual identity present in the current screenshot, but allow these patterns to deform and interact with other modes.

**C. Angular and asymmetric modes**

Patterns with multiple lobes, diagonal boundaries, bent axes, uneven regions, and broken symmetry.

These prevent the scene from looking like a perfectly symmetric procedural flower at all times.

**D. Fine, high-order modes**

Smaller ripples, intricate interference fringes, narrow ridges, and detailed local oscillation.

These should add detail to the larger structure without replacing it. They are particularly useful for upper-midrange and high-frequency activity.

**E. Traveling and phase-shifted components**

Introduce selected traveling disturbances or phase-shifted patterns that move across the field. Use them to animate the evolution of the standing-wave structures rather than making every part of the surface oscillate in perfect synchrony.

Not every mode needs to travel. The contrast between stable-looking resonant structures and evolving interference is important.

### Mode superposition

The active surface should be the combined result of several simultaneously contributing modes.

For each mode, conceptually evaluate:

    contribution_i = spatial_pattern_i(x, y)
                 * amplitude_i(t)
                 * temporal_phase_i(t)

Then combine contributions with carefully controlled weighting and nonlinear shaping where appropriate.

Avoid a simple sum that produces runaway heights, excessive cancellation, or a surface that looks like random noise.

Normalize and constrain the result so that the terrain maintains coherent visual scale while retaining dramatic changes.

Use smoothly varying envelopes and phase relationships. Do not randomly regenerate the mode pattern every frame.

### Evolving patterns

The scene should not remain locked into the same radial configuration throughout an entire song.

Allow the relative contributions and phase relationships of the modes to evolve in response to musical energy, sustained excitation, and changes in spectral content.

The spatial structure should transform continuously. For example, a broad radial mode may dominate a sustained low passage, while overlapping higher modes introduce more intricate boundaries during a dense passage.

Do not switch abruptly between a few predefined shapes. The system must interpolate continuously between different configurations.

Use deterministic, smoothly evolving modulation where possible. Identical input and initial conditions should produce reproducible behavior for debugging.

## 4. Make the audio drive the resonant medium, not just its appearance

Use AV Gen's existing audio-analysis and signal-routing architecture. Reuse established signal-bus, modulation, smoothing, and parameter systems rather than introducing a separate audio pipeline.

Map audio features to meaningful aspects of the resonant system.

### Bass and low-frequency energy

Drive:

- Large-scale displacement amplitude.
- Broad basin and ridge movement.
- Low-order modal excitation.
- The strength of large-scale interference.
- The persistence of slowly decaying deformation.

A strong kick should create a noticeable physical response across a region of the surface. It should not merely illuminate an existing hollow.

### Midrange energy

Drive:

- Medium-scale modes.
- Changes in the relative strength of angular and radial patterns.
- Local curvature and ridge migration.
- More complex interaction between overlapping structures.

Midrange activity should make the world structurally richer, not simply brighter.

### High-frequency energy

Drive:

- Fine surface vibration.
- Smaller-scale modes.
- Subtle traveling ripples.
- Fine interference patterns and surface glints.

High-frequency detail must remain subordinate to the large-scale geometry.

### Transients

Use onset or transient information where already available in the engine.

A kick or snare should excite the field in a spatially coherent way. This can be implemented using localized modal excitation, a propagating disturbance, or a combination.

Each transient should have:

- A clear initial response.
- A short, controllable rise.
- A natural decay.
- A spatial footprint.
- A contribution to the underlying geometry.

Different transient types may have different response envelopes, but avoid making every kick produce the same perfectly circular crater.

### Sustained energy and release

Sustained notes and builds should progressively excite relevant modes. When the musical energy decreases, the field should relax and continue to resonate briefly before settling.

Implement smoothed envelopes or damped modal responses. Avoid direct, unsmoothed mappings from instantaneous FFT bins to vertex positions.

Quiet passages should remain visually beautiful, but the world should not stay at maximum deformation regardless of the input.

### Separate physical response from visual intensity

Keep these concepts distinct:

- Displacement amplitude.
- Modal excitation.
- Fine-scale vibration.
- Surface emission.
- Material color.
- Transient illumination.

They may be correlated, but they should not all be driven by the same scalar.

This separation is essential to prevent a loudness-reactive color effect from masquerading as dynamic geometry.

## 5. Use physically inspired behavior without requiring an expensive physical solver

The scene does not need to solve the full elasticity equations of a real plate.

The objective is an artistically controlled, physically inspired resonant field with coherent nodal patterns, believable temporal behavior, and strong musical responsiveness.

Evaluate these implementation options against the current engine:

### Option A: Analytic modal field

Evaluate a collection of procedural spatial modes and their time-varying amplitudes and phases.

Advantages:
- Predictable behavior.
- Low computational cost.
- Easy to link directly to audio features.
- Straightforward to debug and artistically tune.

Risks:
- Can look overly mathematical or repetitive if the mode families are too simple.
- Requires care to produce rich interactions and avoid a recognizable looping pattern.

### Option B: GPU field simulation

Maintain a low- or medium-resolution dynamic field and update it over time using GPU simulation, potentially with wave propagation, damping, and external excitation.

Advantages:
- Natural propagation and interaction.
- Local disturbances can travel and interfere.
- Persistent excitation can produce evolving structures.

Risks:
- Additional simulation cost.
- Potential instability or numerical artifacts.
- More complexity in synchronization, resolution, and boundary conditions.

### Option C: Hybrid modal and simulated field

Use a coherent modal foundation for large-scale structure, with a lower-resolution evolving field contributing local propagation and secondary deformation.

This is a promising direction if it provides a substantial visual improvement within the live performance budget.

**Do not choose the most complicated implementation by default.** Prototype the simplest approach capable of producing the required behavior, then compare it with alternatives using actual captures and GPU timings.

A full finite-element or physically accurate plate simulation is not required. Do not spend the project implementing scientific accuracy at the expense of visual quality.

## 6. Geometry must support the new behavior

The current screenshot shows jagged crater rims and soft foreground geometry. The new deformation system must address these issues rather than amplify them.

### Resolution and topology

Inspect the current mesh density and vertex distribution, especially in the foreground.

Choose an appropriate solution based on the engine's capabilities:

- A sufficiently dense procedural grid.
- Adaptive or multiresolution geometry.
- A higher-resolution displacement representation.
- A GPU-generated or GPU-displaced surface.
- Another approach compatible with the existing renderer.

The geometry must support the smallest displacement features that are visibly important at the camera's current distance.

Do not simply multiply the existing mesh resolution indiscriminately. Establish where additional resolution is needed and measure its cost.

### Smooth, continuous surfaces

Eliminate visible faceting and jagged edges along the deformed rims.

Check:

- Vertex density relative to screen-space size.
- Interpolation of the displacement field.
- Normal reconstruction.
- Temporal stability.
- Aliasing in high-frequency deformation.
- Silhouette quality.

Fine modes should be attenuated or filtered when their spatial frequency exceeds what the current geometry and pixel footprint can represent.

### Surface normals

Normals must respond correctly to the changing geometry.

If the displacement is shader-based, use a robust method to obtain the displaced surface normals, such as analytical derivatives or finite differences, depending on what is appropriate for the implementation.

If geometry is updated explicitly, ensure the normals and tangents remain synchronized with the deformation.

Stale normals will make the surface look like a moving texture applied to static geometry.

### Preserve the close, immersive framing

Keep the camera looking down into a continuous, ground-filling world.

Do not add a horizon, sky, architecture, crystals, organisms, decorative walls, or unrelated objects.

The deformation should extend across the entire visible surface. The foreground must exhibit meaningful motion rather than acting as a static, blurry frame around the interesting center.

The camera should make the world feel expansive, not like a small object sitting inside a radial crater.

## 7. Nodal structures must be visible in the geometry

A critical characteristic of cymatics is the relationship between moving regions and nodal regions.

In a real vibrating plate, nodal lines are locations of little or no displacement for a particular mode. They are not simply bright outlines painted onto a surface.

Use the simulated modal field to derive:

- Areas of high displacement.
- Areas of low displacement.
- Boundaries between regions of opposite displacement.
- Regions of strong modal interference.
- The evolving position of nodal structures.

The combined field will not necessarily have the same nodal lines as any one constituent mode. Make sure the visual system can represent that distinction.

Nodal regions should influence the actual shape, not just its color. They may appear as stable ridges, narrow seams, valleys, or transitions depending on the chosen field and its geometry.

Do not hard-code the existing crater rims as permanent nodal boundaries.

Avoid a uniform, glowing outline around every hollow. The most visually interesting boundaries should emerge from the changing field.

## 8. Make the material response follow the same simulation

Retain the existing plasma palette and lighting direction unless testing demonstrates a clear improvement.

The current cyan/teal ridges, dark navy recesses, and fuchsia interior accents provide a useful visual identity.

The next step is to make the material respond to the evolving resonance.

Drive material attributes from meaningful properties of the field:

- Displacement magnitude.
- Local oscillation amplitude.
- Spatial gradients.
- Curvature.
- Modal interference.
- Transient excitation.
- Whether the surface is approaching or leaving an extreme.

Possible responses include:

- Cyan-white highlights tracing selected low-motion boundaries.
- Fuchsia appearing in regions of intense modal interaction.
- Teal highlights migrating along moving slopes.
- Subtle spectral changes across opposing displacement regions.
- Local emission that rises and decays with resonant excitation.
- Specular highlights moving naturally as the geometry changes.

Do not assign an arbitrary rainbow to the field. Preserve color continuity and a clear luminance hierarchy.

Keep the dark recesses dark enough to establish depth. Do not illuminate every region equally.

The lighting and material response should make the motion easier to read, but the geometry must remain compelling with emission temporarily disabled.

## 9. Add a restrained layer of secondary motion

After the primary geometry works, consider secondary phenomena that reinforce the resonant material.

Potential additions include:

- Fine ripples moving away from a transient's excitation region.
- Small, short-lived surface oscillations riding on a large deformation.
- Subtle highlight waves that follow changing curvature.
- Fine surface detail that appears to vibrate with the underlying material.

These must be derived from, or coordinated with, the main simulation.

Do not add particle storms, floating objects, crystals, organisms, shards, or generic audio-reactive decorations.

Do not use particles to conceal weak geometry.

Secondary motion is optional. A compelling primary surface is the acceptance requirement.

## 10. Avoid these failure modes

Explicitly reject the following outcomes:

- The current geometry with more vertical displacement added.
- A fixed radial crater pattern that merely gets deeper and shallower.
- A height field made from unrelated sine waves without meaningful modal structure.
- Generic procedural noise that makes the world look like boiling terrain.
- Uniform pulsing of the whole scene.
- Arbitrary random motion on every frame.
- A few preset shapes that snap or morph abruptly between states.
- A displacement effect so subtle that the geometry appears static at normal playback speed.
- High-frequency noise that overwhelms the broad structure.
- Audio-reactive colors that conceal a largely unchanged surface.
- A simulation whose geometry moves but whose normals, highlights, and shadows do not follow.
- A sophisticated simulation that misses the live frame budget.
- A camera change that removes the ground-filling composition that currently works.

Do not declare success because the scene responds to a kick in a single screenshot. Evaluate continuous motion over actual music.

## 11. Implementation sequence

Work in explicit, reviewable stages. Capture intermediate results so that visual improvements and regressions are attributable.

### Pass 1 — Diagnose and establish a baseline

Document the existing geometry, animation, audio mapping, camera, and performance characteristics.

Identify the smallest architectural change that can deliver genuinely evolving geometry.

### Pass 2 — Build a geometry-only prototype

Implement the core resonant field and make it deform the surface.

Temporarily disable emission and reduce the importance of color effects so the geometry can be evaluated in isolation.

Capture several seconds of animation.

The prototype passes only when the surface clearly changes its structural configuration over time.

### Pass 3 — Establish coherent modal behavior

Tune the spatial modes, phase relationships, amplitudes, and temporal envelopes.

Demonstrate broad and fine motion coexisting, with patterns that evolve continuously rather than simply oscillating as one static shape.

### Pass 4 — Connect the field to audio

Integrate the existing AV Gen analysis and modulation pipeline.

Test bass, midrange, high-frequency activity, transients, sustained notes, and quiet passages.

Adjust mappings so each frequency range contributes at an appropriate spatial scale.

### Pass 5 — Improve geometry quality

Resolve the foreground softness, rim artifacts, normal quality, and temporal aliasing.

Do not sacrifice the new motion to preserve an inadequate mesh representation.

### Pass 6 — Restore and refine the plasma art direction

Re-enable the cyan/fuchsia lighting and material responses.

Make sure the material is following the changing geometry and that the resulting highlights communicate motion rather than competing with it.

### Pass 7 — Optimize and validate

Measure CPU and GPU costs at the intended live resolution.

Use the engine's existing quality and performance systems where possible. If the simulation needs reduced resolution or simplified modes at lower quality levels, implement a graceful quality scaling strategy.

Retain the current scene's performance baseline as a comparison point.

## 12. Required validation captures

Provide continuous captures, not just still screenshots, for the following cases:

1. **Quiet passage:** low-amplitude, restrained motion that remains visually legible.
2. **Sustained bass:** large-scale geometry changes and lingering resonant motion.
3. **Kick-heavy passage:** clear physical excitation with a coherent spatial response.
4. **Snare-heavy passage:** more localized, sharper deformation that differs from the bass response.
5. **Dense full mix:** multiple scales of motion and interacting structures without becoming visual noise.
6. **High-frequency passage:** fine detail that remains subordinate to the larger surface.
7. **Transition or build:** a continuous evolution from one structural configuration to another.

For every capture, compare geometry-only and fully shaded results when useful.

The final review must establish that the world is moving even when the viewer ignores the colors and emission.

## 13. Performance and implementation constraints

- Preserve AV Gen's existing rendering architecture and backend.
- Prefer GPU execution for per-vertex or per-cell field evaluation when appropriate.
- Avoid unnecessary CPU readbacks and per-frame allocation.
- Do not create a high-resolution CPU simulation if an efficient GPU approach is available.
- Keep simulation state and audio analysis synchronized.
- Avoid frame-rate-dependent animation.
- Use stable integration or bounded analytic responses so the simulation cannot explode on transients.
- Profile the actual scene rather than estimating cost from code complexity.
- Keep the live quality setting viable on the currently supported hardware.
- Add tests for any new CPU-side math, parameter bounds, and deterministic field evaluation that can be tested independently.

Do not introduce a new renderer, replace the existing audio pipeline, or refactor unrelated systems for this feature.

## 14. Acceptance criteria

The scene is not complete until all of these conditions are met:

**Structural motion**
- The terrain's topology visibly evolves throughout playback.
- Ridges, hollows, lobes, and nodal boundaries change configuration, rather than simply moving up and down in place.
- Multiple modes overlap to produce coherent interference patterns.
- Motion exists across the full frame, including the foreground.

**Musical response**
- Bass and sustained low-frequency energy produce broad, substantial movement.
- Transients produce clear physical disturbances.
- Midrange and high-frequency content add detail at appropriate scales.
- The surface relaxes naturally when excitation subsides.
- Audio response remains stable and visually coherent.

**Visual quality**
- The existing plasma palette and immersive framing are preserved.
- Geometry and shading remain synchronized.
- Foreground softness and jagged rims are materially improved.
- The result does not resemble generic noisy terrain, a static flower-shaped crater, or an animated color map.

**Live viability**
- Performance is measured at the intended live resolution and compared against the baseline.
- The scene has a documented quality/performance tradeoff where necessary.
- No new instability, major frame-time spikes, or unbounded simulation behavior is introduced.

## 15. Final report

When finished, provide:

1. A concise explanation of the old motion model and why it looked static.
2. The new resonant-field architecture and why it was selected.
3. How spatial modes, interference, transient excitation, and damping work.
4. How audio analysis controls the geometry.
5. Before/after stills and continuous captures covering the required musical cases.
6. CPU and GPU timings at the relevant quality levels and resolution.
7. Any remaining limitations, artifacts, or follow-up work.
8. Tests run and any changes to scene documentation.

Do not report success solely because the scene compiles, the audio changes the colors, or a single transient moves the surface.

### Final artistic principle

**Cymatic World should look like a continuous world made of resonant matter, with its entire structure being rewritten by music in real time.**

The geometry is the performance. Color and light reveal it. They are not substitutes for it.
