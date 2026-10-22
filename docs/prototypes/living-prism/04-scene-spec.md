# Owner's scene specification (verbatim, 2026-10-22)

Owner's line sent with it: "BLENDER IS AVAILABLE IF YOU NEED TO USE IT TO CREATE ASSETS"

Coordinator notes on the spec's paths (facts checked 2026-10-22):
- the reference folder is `~/Desktop/living prism/` (8 JPEG images); the spec says both `living prism` and `living crystals`, and only the former exists;
- the stems are `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav` (gitignored, owner's commit 5eaedb07), not `audio/feline-footwear-stems`; the mix is `assets/audio/feline-footwear.wav`.

---

# The Living Prism — Complete Art Direction & Scene Construction Specification

**Project:** AV Gen  
**Milestone:** The Living Prism  
**Purpose:** A polished, audio-reactive live audiovisual environment  
**Target:** 1080p, 30 fps live performance, with graceful adaptive quality reduction  
**Primary test material:** `audio/feline-footwear-stems` — drums, bass, and synth stereo stems  
**Visual references:** The user's Desktop folder named `living prism`, plus the crystal-grotto reference images supplied with this brief

---

## 1. Mission

Create a compact, visually rich crystalline grotto in which color, light, crystal, water, and sound appear to form one living system.

The world should feel like an extraordinary hidden place: a small crystalline chamber with dark mineral walls, striking crystal formations, a reflective pool, a defining arch or other memorable structure, and carefully controlled atmospheric depth.

Its scale is deliberately intimate. The camera should stay close enough to the environment that materials, reflections, facets, glints, and ripples become important. Visual richness should come from the quality of the forms, the interaction of light and material, and the evolving musical response—not from an enormous world or hundreds of unique assets.

The defining artistic idea is:

**Music moves through a compact world of crystal and water, changing its color, exciting its surfaces, and leaving temporary traces of light.**

This is a live visual instrument, not a static environment showcase. It must respond convincingly to real music, remain readable on a large projector, and sustain interest throughout a performance.

## 2. Read the reference material first

Before constructing the scene, inspect the user's Desktop folder named `living crystals`.

- Examine all available concept images and identify recurring visual qualities.
- Treat the references as inspiration, not assets that must be reproduced literally.
- Extract a coherent visual language: crystal silhouettes, facet scale, color relationships, lighting, material properties, environmental density, water treatment, and atmosphere.
- Determine which references complement one another and which should not be combined.
- Use suitable references to establish a consistent look across the entire scene.

The owner has supplied three key visual directions:

1. **Compact crystal grotto:** colorful purple, blue, and pink crystal formations above serene reflective water, framed by dark rock and a memorable focal structure.
2. **Material richness:** distinctive crystal surfaces with strong facets, vivid internal color, sharp highlights, selective translucency, and luminous seams.
3. **High visual activity in a small space:** light fronts, ripples, glints, particles, and palette transitions make a small number of hero formations feel alive.

Use these as the artistic foundation. The final scene should have its own uniform identity rather than looking like unrelated reference images assembled together.

The agent may research additional visual references and suitable freely usable assets. Favor references and assets that help produce a distinctive, cohesive, technically achievable real-time scene. Record licensing and attribution requirements for any external assets used.

Do not spend excessive time collecting references. Once the look is sufficiently understood, move to a blockout and validate it in AV Gen.

## 3. Artistic direction

### 3.1 Overall look

Aim for a vivid, dreamlike crystalline grotto with a balance of natural geology and impossible mineral beauty.

The scene should combine:
- Deep indigo and violet environmental shadows.
- Saturated pink, magenta, blue, and purple crystal formations.
- Selective amber, coral, or gold accents for contrast.
- Bright, refracted-looking highlights and luminous mineral seams.
- Dark rock that gives bright materials a clear silhouette.
- Reflective water that visually connects the environment.
- Restrained atmospheric haze and localized light shafts.
- Small animated details that make the world feel alive between major musical events.

Use color boldly. The scene should be capable of moving through striking, high-saturation looks without defaulting to indiscriminate rainbow cycling.

The aesthetic should feel like polished contemporary real-time VFX: expressive, tactile, spatially coherent, and visually intentional.

### 3.2 Avoid

Do not create:
- A monumental landscape requiring a large environment-art production.
- A huge crystal cavern that depends on miles of background geometry.
- A featureless black void with floating objects.
- A generic realistic cave with crystals pasted onto its walls.
- A uniformly glowing environment where every surface competes for attention.
- Identical crystal spikes repeated across the scene.
- Randomly spinning primitives or arbitrary geometry deformation as the main attraction.
- Constant full-screen flashing, glitching, or chromatic aberration.
- An always-on rainbow filter.
- A composition that only looks good from one frozen camera angle.
- An asset-heavy scene whose detail cannot be maintained at the live performance target.

The goal is not maximal object count. It is maximum visual richness per carefully constructed element.

## 4. Environment scale and composition

### 4.1 The compact grotto

Build one cohesive, enclosed or partially enclosed crystalline environment.

The minimum composition should include:

1. **A central reflective pool** or shallow water surface.
2. **Three to five major hero formations**, distributed around the focal composition.
3. **A small number of supporting crystal clusters** integrated into rock and ledges.
4. **One memorable arch, mineral bridge, or equivalent focal structure.**
5. **Dark geological masses** that frame the scene and give the crystals contrast.
6. **A limited set of smaller mineral details** for glints, local responses, and depth.
7. **A carefully controlled background** using overlapping formations, rock silhouettes, haze, and lighting rather than a huge modeled environment.

These are initial composition targets, not hard engine limits. Change the count if the actual blockout demonstrates a better composition.

The hero formations should be visually distinct in silhouette, scale, orientation, and material treatment. Avoid making all of them variations of the same upright spike.

### 4.2 Composition hierarchy

Every representative camera frame should have a clear hierarchy:

- **Primary focal element:** the most visually distinctive crystal cluster or illuminated structure.
- **Secondary elements:** other formations that frame the focal point or establish the space.
- **Reflective element:** the pool or another deliberately positioned surface.
- **Environmental foundation:** dark rock and mineral walls.
- **Fine detail:** small glints, luminous seams, particles, ripples, and trails.

Keep enough dark or low-detail material around the brightest features for contrast. The scene should not become an undifferentiated mass of luminous facets.

Use foreground silhouettes and overlapping formations to imply depth. The viewer should be able to understand where the camera is relative to the major objects.

### 4.3 Camera

Use a restrained, continuous camera route through the compact environment.

Suitable movements include:
- A slow orbit around the main formation.
- A gentle dolly toward or past the crystals.
- A short passage between two formations.
- A gradual rise or descent revealing the pool and focal structure.
- Subtle parallax from foreground elements.

Combine these into a smooth, repeatable trajectory. Avoid rapid spinning, constant camera shake, aggressive fisheye distortion, or a long flythrough that implies a much larger environment than the scene contains.

The route should expose multiple useful compositions without requiring a large collection of unique environments.

Ensure loop continuity, stable orientation, and smooth camera velocity. The camera should remain interesting when the music is quiet, without depending on constant motion.

## 5. Material and screen-space budgets

These are starting budgets for the first blockout. Measure the actual scene and revise them only with a documented artistic or performance reason.

| Feature | Initial target |
|---|---|
| Translucent or refractive crystal surfaces | Approximately 5–12% of the visible screen area in representative shots |
| Reflective water and polished surfaces | Approximately 10–20% of the visible screen area, predominantly the pool |
| Particle coverage | Typically below 2–4% of screen pixels at once, including bursts and trails |
| Main visual anchors | One primary focal region and a few supporting formations |
| Full-screen feedback | Not part of the initial scene |
| Full-screen palette and grading changes | Allowed, but controlled and musically motivated |

The percentages describe visible screen coverage, not the percentage of objects or triangles in the scene.

The targets are intended to prevent the scene from becoming expensive or visually muddy. A transient may briefly exceed the particle budget if it creates a particularly strong musical accent, but sustained overdraw should be avoided.

### 5.1 Opaque materials

Most of the environment should remain opaque. Rock, dense crystal, and much of the supporting geometry should provide stable silhouettes and reliable depth.

Use the existing procedural geometry, instance system, LOD, material programs, effectors, and rendering pipeline.

### 5.2 Translucent crystal

Translucency is a deliberate accent.

Use it on selected hero formations where it adds a visible benefit: color transmission, apparent depth, internal glow, or a distinctive interaction with the background.

Use the planned screen-space refraction implementation when it becomes available. Gate the effect per material and provide a quality-ladder control.

**Do not require one crystal to be seen through another crystal in the first version.**

Arrange translucent formations against dark rock, water, open gaps, or other visually useful backgrounds. Use material contrast, facet lighting, and emissive accents to create depth without relying on multilayer transparency.

If the blockout cannot achieve the desired look without overlapping transparent layers, demonstrate the problem with an actual render before proposing a more advanced transparency technique.

### 5.3 Reflections

The pool is the principal reflective surface. A few polished crystal surfaces may also use reflections if they materially improve the composition.

Start with the existing image-based lighting and reflection capabilities. Evaluate material-gated, half-resolution screen-space reflections only after the blockout is representative.

Measure the effect independently. Keep it optional in the quality ladder, and preserve the scene's identity when it is reduced or disabled.

### 5.4 Emissive materials

Use emissive detail to make the crystal feel active:
- Narrow luminous seams.
- Selected bright facets.
- Internal cores or inclusions.
- Small regions of colored light near the base of formations.
- Temporary accents activated by musical events.

Avoid uniform emissive coverage. Let the light reveal the geometry rather than replace it.

Use existing bloom conservatively. Facet edges, highlights, and material separation must remain readable at high brightness.

## 6. Palette design and color modulation

Color modulation is a primary VFX goal, not a secondary polish pass.

Create a cohesive family of palette looks using the existing AV Gen palette system and OKLab blending. The palette should influence crystal materials, selected rock accents, emissive seams, lighting, and water reflections in a coordinated way.

### 6.1 Initial palette families

Use these as starting points and refine them against actual renders.

- **Ultraviolet:** indigo, violet, fuchsia, electric pink.
- **Glacier:** cobalt, electric blue, icy lavender, pale blue-white highlights.
- **Solar mineral:** magenta, coral, amber, molten gold, warm white.
- **Opaline:** rose, periwinkle, pearl, iridescent violet.
- **Abyssal:** deep blue, violet, restrained cyan, luminous blue highlights.
- **Wild spectrum:** multiple vivid color families distributed intentionally across different objects and regions.

Do not make all colors equally bright or equally saturated. A strong palette needs contrast and a visual hierarchy.

### 6.2 Palette behavior

The world should be able to undergo dramatic visual transformations while remaining recognizable as the same grotto.

- Continuous audio modulation may shift palette balance, saturation, warmth, or emissive intensity.
- MIDI should let the performer select or deliberately blend palette looks.
- Transients may create temporary local color accents.
- Sustained harmonic or tonal activity may gradually reshape the overall color balance.
- Bass may intensify selected environmental elements without forcing a palette change.
- Quieter passages should allow softer, more atmospheric looks.

Avoid mapping raw audio amplitude directly to every color channel. Use conditioned signals and the existing route chain to make transitions intentional and stable.

Avoid constant automatic cycling. A color transition should have a reason, a destination, and a suitable timescale.

## 7. Audio-reactive behavior

The scene must feel like a cohesive instrument, not a collection of unrelated audio bindings.

Use the existing signal bus, `response.*`, `stem.*`, `control.*`, beat and timeline signals, processor chains, fields, grids, particles, effectors, material programs, and palette system.

Use the planned same-frame conditioned `signal.*` capability when it becomes available. It should provide reusable named musical drivers without introducing another modulation framework.

### 7.1 Transients and drum impacts

This is the scene's most important fast response.

Use broadband onset signals and onset-frame register information for immediate attacks. Prefer the drum stem when it is available. Do not depend on slow mix kick/snare/hat classifications to trigger core live impacts.

Possible responses:
- A sharp emissive accent on a nearby crystal cluster.
- A ripple expanding across the pool.
- A brief wave traveling through selected mineral seams.
- A compact particle or glint burst.
- A short-lived highlight sweeping across nearby facets.

Keep the visual origin clear. An event should read as an impact at a particular region, not as a global brightness flash.

Use distinct response shapes for different intensities or frequency registers when the signals support them. Do not make every drum hit produce the entire effect vocabulary.

### 7.2 Bass

Bass should create broader and slower environmental motion.

Potential responses include:
- Pulses moving through the largest crystal formations.
- Slow changes in emissive seam intensity.
- A broad disturbance across the pool.
- Subtle deformation or expansion of selected forms.
- Increased visual energy in the current palette.

Distinguish sustained bass presence from isolated transients. Use conditioned signals and smoothing rather than applying raw bass amplitude independently to every parameter.

### 7.3 Synth and harmonic activity

The synth stem should provide a distinct visual voice.

Use sustained and tonal content to influence:
- Palette balance and saturation.
- Crystal internal light.
- Facet highlights and material accents.
- Small secondary glints and fine particles.
- Slow evolution of the grotto's visual character.

Do not spawn particles for every note. The scene should respond to the continuous character of the synth part as well as to selected events.

### 7.4 Overall energy and phrase evolution

Use a small set of meaningful scene-authored conditioned signals. Candidate signals include:
- Overall musical energy.
- Bass presence.
- Transient activity.
- Harmonic brightness.
- Slowly accumulated section intensity.

These are scene-level definitions, not requests for new engine-wide signal families.

Let the world evolve over several seconds. The active palette may become more vivid, selected formations may brighten, and the number or prominence of light fronts may increase.

When the music quiets, allow the scene to settle naturally. Do not abruptly reset the entire world unless the musical transition calls for it.

### 7.5 Sound-to-light timing

Fast impacts should use the shortest reliable path from the onset source to the rendered result.

Avoid unnecessary macro hops or copied modulation chains. Verify that the relevant signal reaches its final parameter in the intended frame.

The audit proposes a sound-to-light target of no more than 45 ms p50 and 60 ms p95 at 30 fps. Measure this through the corrected profiling and loopback tooling where available, and report the actual results.

Do not claim that the target is met based solely on route timing or visual inspection.

## 8. Spatial effects, propagation, and temporal memory

### 8.1 Radial and surface-wide effects

The default propagation model should be simple and readable.

A musical event can create a localized impact followed by an expanding ripple or front that spreads outward across the pool, nearby formations, or another suitable region.

Reuse existing onset, wave, spectrum, and grid propagation mechanisms. Use the existing distance-based delay where appropriate.

Control propagation speed, intensity, and decay so that multiple events can overlap without overwhelming the scene.

### 8.2 Propagation along structures

A limited number of effects may travel along the defining arch, selected crystal seams, or another visually continuous structure.

This is a secondary signature effect, not a requirement for a new general-purpose engine system.

Start with a scene-specific implementation using existing geometry and an appropriate stored distance or instance attribute if needed. Compare the result with a simpler radial or surface-wide effect.

Only retain structure-following propagation if it visibly improves the scene.

### 8.3 Persistent visual memory

**Use decaying traces, not full-screen echo or feedback.**

A transient can leave a luminous seam or faint glint. A traveling front can leave a subtle trail. Pool ripples can fade over time. These remnants should make the world feel responsive to the recent musical past.

Use existing particle trails, grid decay, onset decay, and envelope primitives. Use conditioned signals for named shared accumulation where useful.

Establish several timescales:
- Immediate impacts: sharp and brief.
- Traveling fronts: visible for the duration of propagation, then fading.
- Material accents: lingering but restrained.
- Phrase-scale energy: slower evolution over several seconds.

All accumulated effects must decay predictably. Avoid indefinite accumulation, uncontrolled brightness, and unbounded particle or trail growth.

Do not add full-screen feedback, recursive image accumulation, or elaborate echo effects to the initial implementation. Existing post-processing remains available if a later art-direction decision demonstrates a specific need.

## 9. MIDI controls

MIDI should let the performer shape the scene deliberately.

Use the existing control bus and palette controls. Follow the project's current MIDI mapping conventions rather than inventing a parallel system.

Proposed semantic controls:

1. **Palette:** select or blend the active color look.
2. **Impact intensity:** adjust the prominence of transient effects.
3. **Propagation:** control the strength or prominence of traveling fronts.
4. **World energy:** bias the intensity of broad environmental responses.
5. **Memory:** adjust the persistence of selected visual traces within safe limits.

These are proposed roles, not fixed CC numbers. Keep the initial mapping compact. Implement only controls that are meaningful and can be tested.

Where MIDI soft takeover is available, use it to avoid unexpected parameter jumps after a control change. Do not make the scene depend on unimplemented MIDI features.

Test palette selection and at least one other meaningful MIDI control during actual playback.

## 10. Input configuration and test material

### 10.1 Initial source configuration

Use the existing `Feline Footwear` stems in the repository, expected under:

`audio/feline-footwear-stems`

The set contains stereo WAV stems for:
- Drums.
- Bass.
- Synth.

First inspect the actual files, duration, sample rate, channel layout, synchronization, levels, and musical content. Do not assume that the file names alone establish their exact behavior.

Use the stems as synchronized analysis sources while preserving the intended stereo playback path. Do not mix the stems together merely to create separate visual signals, and do not introduce source separation.

Map each stem to the appropriate visual role:
- Drums → fast impacts, onset-driven ripples, and selected particle or glint bursts.
- Bass → broad environmental energy and slower motion.
- Synth → palette, material response, and sustained visual evolution.

Use existing stem and per-channel signal facilities. Do not create new audio input infrastructure unless the implementation audit proves it is necessary.

### 10.2 Multi-input live testing

For this milestone, the separated stems are the primary test setup for instrument-aware behavior. They should exercise the existing per-source signal path and establish a clear mapping from instrument to visual response.

The scene must also work with an ordinary stereo mix. When separated inputs are unavailable, fall back to reliable shared response signals and broadband onset detection. Do not make false claims about individual instruments when only a mixed signal is available.

A later electronic/EDM track with new stems will be supplied by the owner. Do not block initial implementation on that future material. Keep the mappings easy to retune and use the current test track to expose the first iteration's strengths and weaknesses.

### 10.3 Tempo source

Use the existing MIDI clock path when reliable synchronization to the performance rig is required.

Do not require Ableton Link. Do not make the core visual identity dependent on exact beat tracking from the stereo mix.

Where MIDI clock is unavailable, use the existing beat tracker conservatively and verify behavior when tempo confidence is poor or tempo is ambiguous.

### 10.4 Silence and missing sources

Test:
- Normal stem playback.
- A missing drum, bass, or synth source.
- A silent or near-silent source.
- A stereo-only mix.
- Abrupt transitions from loud to quiet material.

Missing stems must fall back safely. No signal may become permanently latched, and the environment must not remain at full effect intensity after the music stops.

## 11. Rendering and implementation boundaries

The existing renderer and VFX vocabulary are already strong. This milestone should primarily be scene construction and musical mapping.

Reuse:
- Procedural geometry and instancing.
- Existing LOD and GPU culling.
- Compiled material programs.
- Clustered lighting, shadows, fog, and volumes.
- Existing emissive and bloom support.
- Existing fields, grids, and propagation.
- `ParticleRenderer`, trails, ribbons, and effect triggers.
- Existing material effectors and FXL lanes.
- Palette looks and OKLab transitions.
- The existing live quality ladder and profiler.

Do not create a third particle system, a new modulation framework, or a new general-purpose effects framework.

### Rendering prerequisites

Coordinate with the foundation work identified in the audit:
- Honest GPU timings and profiling records.
- Corrected `EnvelopeSource` behavior.
- Conditioned same-frame `signal.*` sources.
- Screen-space refraction for selected procedural materials.
- A spatial upscaler for lower quality rungs, if available in time.

These are shared engine capabilities. Integrate with their actual implementation status; do not duplicate them inside the scene.

SSR should follow a representative blockout and independent cost measurement. A more advanced temporal antialiasing approach is not a prerequisite for the first version.

### Keep scope focused

Do not:
- Rewrite the renderer.
- Rebuild the modulation architecture.
- Add a new general-purpose VFX framework.
- Introduce a general graph-propagation engine.
- Replace working analysis with a neural detector.
- Port unrelated scenes onto new signal abstractions as part of this scene's work.
- Make speculative renderer optimizations without measured evidence.

If a requirement appears impossible with the current capabilities, identify the exact limitation, show the relevant code or profiling evidence, and propose the smallest viable extension.

## 12. Performance requirements and test windows

### 12.1 Target

The live target is 1080p at 30 fps, with stable pacing and graceful reduction when the scene exceeds the available budget.

The scene must preserve its core identity at lower quality rungs. The central crystal silhouettes, palette behavior, and essential impact responses should survive quality reduction wherever possible.

### 12.2 Typical and heaviest moments

Do not invent fixed timestamps for the musical windows before inspecting the stem content.

First analyze `Feline Footwear` and identify two reproducible test windows:

**Typical window**
- Representative musical density.
- Moderate number of active effects.
- Normal camera composition.
- Typical reflection, refraction, and particle coverage.
- Sustained enough to evaluate continuous modulation and ordinary frame pacing.

**Heaviest window**
- The densest or most demanding musical passage supported by the stems.
- Overlapping drum impacts.
- High sustained bass or synth energy.
- The greatest expected number of simultaneous ripples, fronts, particles, trails, and material responses.
- The camera view that exposes the greatest relevant screen coverage or rendering cost.

Use actual analysis and an initial playback/profiling pass to select these windows. Record exact start and end times, stem state, camera position or route progress, and the conditions used to reproduce them.

Also test a quiet passage, an abrupt transition, and a palette change. These are functional tests, not substitutes for the two performance windows.

### 12.3 Budgeting and quality reduction

Geometry is a significant fixed cost in this renderer, so keep the environment compact and use LOD and instance budgets.

Measure:
- Typical and worst-case GPU frame times.
- CPU frame time.
- Live deadline misses and p99 frame interval.
- Quality-rung distribution.
- Refraction and SSR cost independently.
- Material-program cost in the lit scene pass.
- Particle and volume costs.
- The effect of quality changes on the scene's visual identity.

Give each newly added shared rendering effect its own appropriate quality-ladder control. Avoid a scene where a single expensive effect forces the entire renderer down to its lowest rung.

Use the corrected profiler and compare against the existing committed live-scene baseline. Report actual numbers and the test conditions. Do not declare success from a short subjective preview.

## 13. Implementation sequence

### Stage 1 — inspect and establish the visual language

1. Inspect the Desktop `living crystals` folder.
2. Inspect `Feline Footwear`'s three stems and confirm their synchronization and contents.
3. Review the existing scene and renderer conventions, including material programs, palette controls, fields, grids, particles, and camera routes.
4. Establish a short visual reference sheet and a compact palette proposal.
5. Identify any required rendering features that are still in progress.

Deliver a concise initial plan, then proceed to the blockout without waiting for another scene specification.

### Stage 2 — compact environment blockout

Build the central pool, dark mineral framing, a few hero crystal formations, and the defining structure.

Use simple materials initially. Establish:
- A strong camera composition.
- Clear spatial depth.
- A recognizable focal point.
- A loopable camera route.
- A manageable geometry and instance budget.

Do not build minor details until this blockout reads as a coherent environment.

### Stage 3 — hero materials and lighting

Develop the rock, dense crystal, translucent crystal, emissive seam, and reflective pool materials.

Introduce selective refraction when the shared renderer implementation is ready. Evaluate reflections separately.

Build the primary lighting and palette looks. The scene should already look distinctive before the audio-driven effects are added.

### Stage 4 — musical behavior

Implement a small number of end-to-end behaviors in this order:

1. Immediate drum impact.
2. Outward-traveling pool ripple or localized front.
3. Bass-driven large-scale environmental response.
4. Synth-driven color and material response.
5. Persistent traces with predictable decay.
6. MIDI-controlled palette change.

Use conditioned signals to avoid duplicating long chains. Keep each mapping understandable and traceable.

### Stage 5 — detail and visual richness

Add only details that improve the scene:
- Secondary crystal clusters.
- Selective glints and facet highlights.
- Small particles and trails.
- Luminous mineral seams.
- Controlled atmospheric shafts.
- Subtle water details.
- Optional structure-following light paths.

Keep the frame visually legible during the densest musical passage.

### Stage 6 — performance and polish

Capture the typical and heaviest windows. Validate the quality ladder, latency, palette changes, stereo fallback, missing sources, silence, and loop continuity.

Tune composition and effect density before proposing new engine work.

## 14. Acceptance criteria

### Visual quality
- The result is a compact crystalline grotto, not a monumental landscape.
- The pool, hero formations, dark rock, and focal structure form a cohesive composition.
- Materials have distinct identities and do not rely on everything being transparent or emissive.
- Color modulation is bold, controlled, and musically expressive.
- The environment looks compelling in silence.
- The camera reveals enough variation without needing a large world.
- Visual details remain legible on a large projection.

### Musical response
- Drum impacts are immediate, localized, and clearly distinguishable.
- Bass produces broader, slower environmental energy.
- Synth and harmonic content shape palette and material behavior.
- Traveling effects have readable origins, propagation, and decay.
- Temporary traces fade instead of accumulating indefinitely.
- MIDI provides deliberate palette control and at least one additional meaningful control.
- Separated sources improve the scene, while stereo-only operation remains functional.
- Silence and missing sources do not produce stuck or false effects.

### Technical quality
- Existing AV Gen systems are reused wherever possible.
- Refraction is limited to selected materials and has a quality-ladder control.
- SSR is included only if measured value justifies its cost.
- Typical and heaviest windows are reproducible and profiled.
- The scene is tested at 1080p30.
- The implementation reports actual frame-time and deadline results.
- Existing relevant test suites and scene checks pass.
- Sound-to-light latency is measured, with p50 and p95 reported.
- Any missing capability or deviation from the spec is explicitly documented.

## 15. Deliverables

Provide:

1. The working Living Prism scene in the repository's established format.
2. A concise visual direction note derived from the user's references.
3. A list of the hero assets and how they are constructed or sourced.
4. The final palette looks and their intended visual roles.
5. A signal mapping document showing each stem, shared signal, conditioned signal, MIDI control, and visual destination.
6. The typical and heaviest performance-window definitions, including reproducible timestamps and camera state.
7. Performance results at 1080p30, including frame pacing and quality-ladder behavior.
8. A measured assessment of refraction and SSR costs.
9. Test results and known limitations.
10. A list of any requested capabilities still waiting on shared engine work.

Do not create a separate architecture or VFX framework as a deliverable.

## 16. Final creative test

Evaluate the scene in four situations:

**Silence:** the grotto remains a beautiful, coherent environment with readable crystal materials, lighting, and reflections.

**Sparse music:** individual sounds excite specific regions, and the viewer can understand what caused each visual response.

**Dense music:** the environment becomes richer, brighter, and more active without collapsing into an uncontrolled mass of particles and light.

**MIDI palette change:** the same grotto transforms its color identity decisively while retaining its composition and material hierarchy.

The final result should feel like a small, intricately crafted jewel of a world—one whose crystals, water, and light appear to conduct the music itself.
