# AV Gen Art Agent Specification: Digital Ocean

*The owner's brief, verbatim (2026-10-14). A new Sonic Abstract live scene.*

## 1. Mission

Create a new, polished, real-time audiovisual environment for AV Gen called **Digital Ocean**.

Digital Ocean is a stylized, flowing ocean world built from relatively simple geometry, beautiful color palettes, expressive procedural animation, and carefully layered visual effects. It should feel like a complete digital environment that happens to respond to music, not a collection of visualizers placed over a water surface.

Use the existing **Digital Alpine** scene as a creative and technical reference. Inspect its actual implementation, presentation, animation, and audio/MIDI integration before making architectural decisions. Reuse appropriate patterns where useful, but give Digital Ocean its own artistic identity.

The agent has substantial creative freedom. Waves, surf, sky, clouds, a sun, reflections, dolphins, fish, islands, strange marine organisms, geometric sea creatures, particle trails, and other imaginative elements are all fair game. These are possibilities, not a mandatory checklist. Make deliberate artistic choices and prioritize a cohesive result over the number of features.

### Primary objectives

1. Create an immediately beautiful, recognizable ocean environment.
2. Establish continuous, organic motion that remains interesting even when the music is quiet.
3. Make audio reactivity feel intrinsic to the environment.
4. Support meaningful MIDI interaction with expressive visual consequences.
5. Build the scene incrementally, using a VFX-artist-style production workflow.
6. Deliver a performant, reliable live scene suitable for music performance and projection.
7. Finish with a cohesive, polished piece rather than a technical prototype.

## 2. Artistic direction

### Overall aesthetic

Aim for a handcrafted, stylized 3D world with simple geometry, sophisticated composition, rich colors, and strong motion design.

The scene should occupy a compelling middle ground between:

- A beautifully art-directed animated environment.
- A modern generative audiovisual installation.
- A surreal, dreamlike ocean landscape.
- A playable visual instrument that responds to a live musician.

Do not pursue photorealistic water, expensive simulation for its own sake, or a generic AI-generated tropical paradise. The visual quality should come from shape language, color, rhythm, lighting, motion, and composition.

Simple geometry must not mean simplistic art direction. Carefully designed silhouettes, overlapping forms, tonal separation, and restrained detail should make the world feel intentional.

### Color exploration

Experiment with several related palettes before committing. Build actual scene previews and compare them rather than selecting colors based on descriptions alone.

Potential directions include:

**A. Electric twilight**
- Deep indigo and midnight blue water.
- Turquoise and cyan wave highlights.
- Violet and lavender clouds.
- A warm peach or coral sun.
- Pale mint foam and luminous spray.

**B. Surreal tropical**
- Saturated teal water.
- Ultramarine shadows.
- Bright aqua surf.
- Tangerine, pink, or golden sunlight.
- Soft lavender and cream clouds.

**C. Bioluminescent midnight**
- Nearly black navy water.
- Emerald, cyan, and blue-green luminous organisms.
- Violet atmospheric forms.
- A pale moon or alien sun.
- Glowing wakes and delicate particle trails.

**D. Graphic ocean poster**
- Broad, simplified color regions.
- Deep cobalt, seafoam, coral, and warm cream.
- Strong silhouettes and elegant gradients.
- Restrained highlights and carefully placed detail.

These are starting points, not a fixed menu. Develop additional palettes if the scene suggests something better.

Select one primary palette and one or two related variations that can transition naturally during a performance.

### Visual principles

- Establish a clear focal hierarchy.
- Use large forms first and small details last.
- Keep the horizon, sky, and ocean composition readable.
- Use contrast to separate foreground, middle distance, and background.
- Prefer purposeful asymmetry over repetitive, evenly spaced decoration.
- Make motion smooth, layered, and directionally coherent.
- Allow moments of visual calm between high-energy events.
- Ensure the scene remains attractive in motion, not just in a screenshot.

Avoid random spinning shapes, primitive-looking placeholder geometry, excessive bloom, noisy particle spam, arbitrary color cycling, and generic sci-fi decorations unrelated to the ocean.

## 3. Production methodology: work like a VFX artist

**Do not attempt to implement the entire scene in one pass.**

Each stage must have a visible deliverable, a reviewable checkpoint, and explicit acceptance criteria. The agent should run AV Gen, inspect the rendered result, capture screenshots or contact sheets, and improve the actual image before moving forward.

### Stage 0 — Reconnaissance and reference gathering

Before implementation:

1. Locate Digital Alpine and inspect its scene assets, scene definitions, generators, animation systems, signal bindings, and MIDI mappings.
2. Determine how the scene is loaded, selected, configured, and rendered.
3. Identify reusable components and established patterns for audio analysis, modulation, parameter binding, procedural geometry, and animation.
4. Inspect the available visual debugging, screenshot capture, and performance profiling tools.
5. Research stylized ocean environments, geometric water, procedural waves, surf animation, graphic color palettes, and generative marine artwork.
6. Identify any relevant existing AV Gen capabilities that could be reused instead of reimplemented.

Produce a short implementation plan with:
- The intended visual concept.
- The selected or shortlisted palettes.
- The proposed camera and composition.
- The core procedural techniques.
- Reusable engine facilities.
- Anticipated performance risks.
- A sequence of checkpoints.

**Checkpoint:** A written art direction and technical plan grounded in the actual repository.

### Stage 1 — Composition and graybox

Build the minimum recognizable ocean environment.

Include:
- A camera with a deliberate viewing angle and composition.
- A large ocean surface extending naturally toward the horizon.
- A sky with a readable horizon and appropriate atmospheric depth.
- A sun, moon, or similarly strong atmospheric focal element.
- A few broad wave forms that establish scale and direction.

Use simple geometry and temporary flat materials. Do not implement dolphins, detailed foam, particle effects, or complicated lighting yet.

Explore at least a few camera compositions:
- A low, cinematic view near the water.
- An elevated view that reveals wave patterns.
- A more graphic, stylized composition with a strong horizon and sun.

Choose whichever produces the strongest result for a live audiovisual scene.

**Checkpoint:** The world must already read as an ocean environment and look compositionally intentional without any decorative effects.

### Stage 2 — Water silhouette and primary wave motion

Develop the ocean as the central visual feature.

Investigate efficient procedural techniques supported by the existing engine, such as:
- A displaced grid or tessellated surface.
- Layered directional wave functions.
- Multiple wave trains with different wavelengths, directions, and amplitudes.
- Broad swells combined with smaller surface undulations.
- A limited number of art-directed hero waves.

Combine slow, large-scale motion with faster, smaller-scale movement. Avoid uniform sine-wave motion across the entire surface.

The water should have an identifiable direction of travel and a pleasing rhythm. Waves should overlap and evolve continuously rather than simply bob up and down in place.

Do not assume that a physically accurate ocean model is required. A stylized approximation that looks excellent and runs well is preferable.

**Checkpoint:** The water is attractive, visibly animated, and convincing within the chosen artistic style even with flat shading.

### Stage 3 — Water color, lighting, and material design

Turn the graybox into a designed environment.

Develop:
- Deep-water color and tonal variation.
- Lighter colors on illuminated wave faces.
- Contrasting crest highlights.
- Sunlight or moonlight reflections.
- Atmospheric color variation near the horizon.
- Subtle translucency-inspired or subsurface-inspired color effects where appropriate.

Explore both broad color bands and more continuous shading. Stylized water can use deliberate color regions to emphasize wave shape.

Consider a controlled sun-glint pattern or elongated reflection that shifts with the surface. Make the reflection an intentional compositional element rather than a generic bright spot.

If the engine supports the required lighting, reflection, and shading features efficiently, use them. Otherwise, build an art-directed approximation rather than introducing a large rendering subsystem.

**Checkpoint:** The water has depth, contrast, a distinct palette, and a strong visual relationship with the sky.

### Stage 4 — Sky, clouds, and atmospheric composition

Develop the sky as a first-class component of the scene.

Potential elements:
- A sun with a beautifully designed disc, halo, or stylized rays.
- Layered clouds with broad, readable silhouettes.
- Slowly drifting cloud banks.
- Atmospheric haze around the horizon.
- Subtle color gradients that establish time of day.
- Optional moon, distant islands, or other carefully selected silhouettes.

Clouds may be procedural meshes, layered shapes, billboards, volumetric effects, or another method appropriate to the engine and performance budget.

Use several scales of cloud motion. Large forms should drift slowly, while smaller details can evolve more quickly.

Clouds must not obscure the composition through excessive density or contrast. Their colors should reinforce the selected palette and help the sun and horizon stand out.

**Checkpoint:** The sky and water feel like one coherent environment, with strong depth and a distinctive atmosphere.

### Stage 5 — Surf, foam, spray, and secondary motion

Add details that make the waves feel alive.

Potential effects:
- Thin, stylized foam ribbons along selected crests.
- Whitewater or luminous turquoise accents.
- Particles that lift off wave peaks and dissolve.
- Small splashes associated with stronger wave events.
- Trails that follow the direction of the water.
- Short-lived sparkling highlights.
- Occasional large, art-directed breaking waves.

Place effects in relation to the underlying wave geometry. Avoid uniformly scattering foam or particles across the entire ocean.

Prefer inexpensive, controlled techniques such as crest masks, instanced geometry, procedural ribbons, or particle emitters over a full fluid simulation.

The scene does not need to simulate every physical interaction. It needs to communicate the sensation of moving water through coherent secondary motion.

**Checkpoint:** Secondary effects enhance the water's form and movement without making the scene visually noisy.

### Stage 6 — Signature set pieces and marine life

Only after the core environment works, introduce one or more memorable features that give Digital Ocean its personality.

Possible features include:

- Stylized dolphins jumping above the surface.
- Schools of fish moving in coordinated formations.
- Geometric whales or surreal marine creatures.
- A luminous manta ray gliding beneath the water.
- Floating rings, shells, or abstract oceanic structures.
- Distant islands or unusual silhouettes.
- A school of glowing organisms rising from a wave.
- A spectacular wave that unfolds during a musical climax.

Dolphins are a particularly promising option, but they must be animated well enough to justify their presence. Their movement should include an arc, a believable leap, a brief moment above the water, and a coherent re-entry or disappearance. A wake or splash can sell the interaction.

Use a limited number of well-designed creatures rather than filling the scene with generic animated assets.

If the agent discovers a more original and visually compelling concept during research or iteration, it should pursue that concept instead.

**Checkpoint:** At least one distinctive visual moment makes the scene memorable, while the underlying ocean remains the main attraction.

### Stage 7 — Audio-reactive behavior

Integrate audio reactivity throughout the scene, not as a final cosmetic pulse.

Use AV Gen's existing audio-analysis and modulation pipeline. Inspect Digital Alpine and other suitable scenes for proven signal bindings before adding anything new.

Potential mappings:

| Musical characteristic | Possible visual response |
|---|---|
| Sub-bass and low-frequency energy | Large swell amplitude, broad water displacement, slow horizon movement |
| Kick transients | Crest flashes, brief wave lifts, coordinated splashes |
| Snare and percussion | Foam bursts, spray, sharp crest highlights |
| Mid-frequency energy | Medium-scale wave detail, cloud movement, water-color modulation |
| High-frequency energy | Small surface ripples, sparkling highlights, fine particles |
| Sustained harmonic energy | Sun halo, atmospheric gradients, cloud coloration |
| Overall loudness | Scene energy, wave intensity, selected secondary effects |
| Onset or transient strength | Discrete visual accents and timed set pieces |

These are candidate mappings. Choose mappings that produce the strongest artistic results.

Important implementation requirements:

1. Use smoothed, normalized signals where appropriate.
2. Avoid directly mapping noisy instantaneous values to every visual parameter.
3. Separate continuous responses from discrete musical events.
4. Use sensible parameter ranges and clamp outputs.
5. Ensure that reactivity remains expressive across quiet passages and loud sections.
6. Avoid synchronized pulsing of every element.
7. Preserve the independent natural motion of waves, clouds, and creatures when no audio is playing.

A kick should not merely make the whole screen flash. It might lift a foreground crest, trigger a small spray event, and briefly illuminate a section of foam, with each response having a different scale and decay.

**Checkpoint:** Playing different kinds of music visibly changes the character of the ocean, not merely its brightness.

### Stage 8 — MIDI as a performance instrument

Make Digital Ocean controllable by a live performer.

First inspect AV Gen's existing MIDI routing, parameter exposure, and modulation architecture. Reuse the established mechanisms wherever possible.

Provide meaningful controls for a practical subset of parameters, such as:

- Wave amplitude.
- Wave speed.
- Wave direction.
- Surface detail or turbulence.
- Foam intensity.
- Sun or atmospheric glow.
- Palette selection or palette blending.
- Particle intensity.
- Marine-life activity.
- Overall scene energy.

Use continuous controllers for expressive parameters and note or trigger events for discrete actions.

Potential MIDI-triggered events include:
- A hero wave rising and breaking.
- A dolphin leap.
- A school of fish passing through the scene.
- A burst of bioluminescence.
- A sun flare or palette transition.
- A short-lived particle or spray event.

These events should have deliberate timing and natural-looking envelopes. A trigger should initiate a complete visual gesture, not just switch an effect on for one frame.

Do not hardcode assumptions about a particular controller. Ensure controls are exposed through the project's existing MIDI and parameter systems where supported.

**Checkpoint:** A performer can meaningfully shape the scene and trigger recognizable visual events without relying on the audio analysis alone.

### Stage 9 — Animation polish and art direction

Review the scene as a continuous piece of animation.

Pay attention to:
- Timing and easing.
- Wave direction and coherence.
- Creature trajectories.
- Cloud drift.
- Reflection stability.
- Particle lifetimes.
- The balance between fast and slow movement.
- How elements enter and leave the composition.
- Whether visual events overlap in a pleasing way.
- Whether the scene has moments of anticipation, release, and calm.

Introduce intentional visual variation. Waves should not repeat in an obvious loop, and signature events should not occur so frequently that they lose their impact.

If multiple camera positions are useful, provide a small number of deliberately designed views rather than arbitrary camera movement.

Prefer a stable, composed default camera with optional subtle movement. The scene should remain readable during live projection and should not require a constantly moving camera to appear dynamic.

**Checkpoint:** The environment feels art-directed and alive, not like a collection of independently animated objects.

### Stage 10 — Visual critique and iterative improvement

This stage is mandatory. Do not declare completion because the scene compiles or renders.

Run the scene and capture actual output under representative conditions:
- No audio.
- Quiet or sparse audio.
- Bass-heavy audio.
- Dense percussion.
- Sustained musical passages.
- MIDI-triggered events.
- Peak visual intensity.

Create a contact sheet of representative frames if the available tooling permits it.

Inspect for:
- Primitive-looking or generic geometry.
- Flat or muddy colors.
- Weak horizon composition.
- Repetitive waves.
- Poorly integrated foam.
- Unconvincing creature movement.
- Excessive glow or particles.
- Visual clutter.
- Flickering, aliasing, or unstable highlights.
- Audio responses that feel arbitrary rather than musical.
- Composition that only works at one moment.

Identify the three most significant visual weaknesses, fix them, and capture another round of output.

Repeat this process until the scene is cohesive and polished.

When possible, use AV Gen's render inspection, profiling, AOV capture, and reference-comparison capabilities rather than relying solely on a single screenshot.

**Checkpoint:** A documented visual critique has led to concrete improvements in the rendered scene.

### Stage 11 — Performance optimization and final validation

Optimize the finished scene rather than prematurely stripping away its artistic character.

Investigate:
- GPU frame time and total frame time.
- Geometry and draw-call counts.
- Shader complexity.
- Particle update and rendering cost.
- Overdraw from transparent effects.
- Excessive bind-group changes or redundant parameter updates.
- CPU-side animation and scene-update costs.
- Scaling behavior at higher output resolutions.

Use existing profiling facilities to identify actual bottlenecks. Do not assume the water shader is the problem without measurement.

Prefer reducing unnecessary work, limiting distant detail, reusing geometry, and simplifying expensive secondary effects before compromising the primary visual quality.

The scene must degrade gracefully when performance limits are reached. Optional particle bursts, distant detail, or secondary effects may be reduced without destroying the composition.

Validate the final scene using AV Gen's actual live rendering path, not just an offline preview.

**Checkpoint:** The scene runs reliably, all major controls work, and performance has been measured and documented.

## 4. Procedural design requirements

The agent should choose the simplest suitable techniques already supported by AV Gen.

### Ocean surface

Favor a layered procedural model that can combine broad swells with smaller waves. A small set of directional wave functions may be sufficient.

If a height field is used, keep the evaluation efficient and consider the visible level of detail. Avoid adding excessive mesh density where it does not improve the rendered result.

If the chosen approach cannot produce convincing crests or breaking waves, fake those elements artistically with separate ribbons, meshes, or particles.

### Foam and spray

Derive foam placement from wave characteristics where practical. Use masks, crest thresholds, procedural geometry, or emitters to create localized effects.

Avoid adding a separate simulation framework just to generate a small amount of stylized surf.

### Clouds and sky

Choose techniques that support smooth motion, palette control, and clean silhouettes. Avoid expensive volumetrics unless profiling and visual results justify them.

### Marine life

Use lightweight rigs, procedural animation, or simple mesh animation where appropriate. Prioritize silhouette, trajectory, timing, and readability.

### Lighting and post-processing

Use the existing renderer and post-processing facilities. Bloom, glow, fog, and reflections should support the art direction rather than substitute for it.

Do not introduce a new renderer or large architectural changes unless the existing engine demonstrably cannot achieve the desired result and the proposed change is justified.

## 5. Live-scene behavior

Digital Ocean must function as a real-time audiovisual instrument.

- The environment must animate when audio is absent.
- Audio reactivity must not be required for the scene to look good.
- MIDI interaction must coexist with continuous procedural animation.
- Visual events must not permanently destabilize the scene's parameters.
- Repeated triggers must not cause unbounded accumulation of entities, emitters, or geometry.
- Parameters must have useful ranges and predictable behavior.
- State must remain stable when audio or MIDI input stops.
- The scene must be suitable for sustained playback, not just a short demonstration.

Where the engine supports configurable parameters, expose artistically meaningful controls rather than dozens of low-level implementation details.

## 6. Suggested initial parameter set

Use these as design targets for the initial implementation, not as a mandate to expose every value.

| Parameter | Purpose |
|---|---|
| `wave_amplitude` | Overall scale of water movement |
| `wave_speed` | Rate of wave propagation |
| `wave_direction` | Dominant direction of travel |
| `wave_detail` | Strength of smaller-scale undulations |
| `foam_intensity` | Amount and brightness of crest foam |
| `surface_shimmer` | Fine-scale highlights |
| `sun_intensity` | Strength of the sun and its glow |
| `cloud_speed` | Rate of cloud drift |
| `palette_blend` | Transition between compatible palettes |
| `particle_intensity` | Amount of spray and secondary particles |
| `marine_activity` | Frequency or intensity of creature appearances |
| `scene_energy` | Coordinated overall intensity control |

Use the actual naming conventions and parameter-binding patterns already established in AV Gen. Do not create redundant parameters where an existing modulation mechanism can achieve the same effect.

## 7. Performance and quality expectations

The project must remain appropriate for AV Gen's live-performance use case.

- Prefer GPU-friendly procedural animation when it is a good fit.
- Avoid unnecessary CPU-side per-frame geometry generation.
- Avoid uncontrolled particle or entity spawning.
- Reuse materials and geometry when practical.
- Keep shader complexity proportional to the visual benefit.
- Profile at the intended live output resolution and at 1080p.
- Record baseline and final GPU/CPU frame times.
- Identify any expensive features and document their cost.

Do not sacrifice the scene's defining visual features merely to achieve an arbitrary optimization target. Instead, identify the most expensive features and optimize them individually.

## 8. Deliverables

The completed work must include:

1. **Digital Ocean scene** implemented within the existing AV Gen project structure.
2. **A distinctive visual identity** with a deliberate palette, composition, and layered motion.
3. **Procedural water and atmospheric animation** that work independently of live input.
4. **Audio-reactive behavior** integrated with AV Gen's existing signal and modulation systems.
5. **MIDI-reactive controls and events** using the project's existing infrastructure.
6. **At least one polished signature visual feature**, such as a dolphin leap, hero wave, or another original set piece.
7. **Visual evidence** captured from the actual running scene, including representative audio-reactive states.
8. **Performance measurements** at representative resolutions.
9. **Documentation** explaining the scene's artistic intent, controls, mappings, and implementation.
10. **A final implementation report** listing completed stages, files changed, validation results, remaining limitations, and future opportunities.

Update any relevant scene catalogs, registries, or documentation that the project uses to make scenes discoverable.

## 9. Acceptance criteria

The scene is complete when all of the following are true:

- [ ] It is immediately recognizable as a stylized ocean environment.
- [ ] Its composition and palette are deliberately art-directed.
- [ ] Water, sky, and clouds have continuous, coordinated motion.
- [ ] Wave forms have sufficient variation to avoid a repetitive, mechanical appearance.
- [ ] Lighting, shading, and color establish depth.
- [ ] Secondary effects enhance the primary forms rather than obscure them.
- [ ] At least one memorable signature event or marine-life feature is implemented.
- [ ] Different musical characteristics produce meaningfully different visual responses.
- [ ] MIDI controls and triggers produce useful, repeatable results.
- [ ] The scene remains attractive when audio is silent.
- [ ] Repeated triggers do not cause unbounded resource growth.
- [ ] Visual inspection and iterative critique have been performed.
- [ ] Performance has been measured on the real rendering path.
- [ ] Existing tests and applicable validation checks pass.
- [ ] Documentation and scene registration are complete.

## 10. Final creative directive

Treat this as a small VFX production, not a programming exercise.

Start with composition. Establish the ocean's silhouette and movement. Find the color palette. Develop the materials. Layer in the atmosphere, foam, particles, and signature elements. Only then refine the musical behavior and performance.

At every stage, look at the rendered result and ask whether it is genuinely beautiful, whether the motion feels intentional, and whether each new feature makes the whole composition better.

Research freely. Explore multiple visual directions. Use the existing engine creatively. If an early concept is not working, discard it rather than polishing a weak idea.

**The final result should feel like an ocean that has become a musical instrument: expansive, fluid, colorful, expressive, and endlessly alive.**
