# Talking Heads: secondary FX art pass + 1-4 dynamic sources (owner brief, verbatim, 2026-10-25)

The owner's covering note: "talking heads need another art pass, for some reason only the bust on the right still draws a drop shadow behind the head - not sure if this is intentional as part of the effect on that entity or what but it should go. mouth animations, eye animations, neck, bust animations, background are all good. we need more unique secondary effects on the busts, spec:"

The owner's screenshot: the screen-right (synth) bust with a darker, offset copy of its head silhouette behind it, reading as a drop shadow (the ghost copies that sit 3 cm behind and offset sideways/back since stop 6). The owner wants it gone.

Coordinator notes: mix kick/snare/hat detection is retired on main; drums-role classes exist only on a drums source. ADR range continues from 1674.

---

# Talking Heads — Secondary Entity FX Art Pass + Dynamic Source Support

## Objective

Perform a focused artistic overhaul of the secondary, sound-source-reactive effects in the existing Talking Heads Sonic Abstract scene.

The scene already has three mannequin busts, animated eyes that dart around the environment, and colored light rays emitting from their mouths. These features are working correctly and must be preserved.

The current weakness is that the secondary effects applied to different busts are too similar. Although the scene uses different audio sources — currently including drums, synth, and bass — the busts respond with nearly interchangeable visual treatments. The drum bust currently has the strongest and most distinctive secondary treatment.

**The primary goal is to make each sound source visually identifiable through its secondary entity effects, not merely through different colors or different amounts of the same distortion.**

Also change the scene to support dynamically between one and four busts, based on the number of assigned sound sources.

This is an artistic implementation task. Inspect the current scene, identify which effects are actually being applied, and improve the rendered result rather than simply adding effect parameters or reporting that more options are available.

## 1. Preserve existing functionality

Do not redesign the scene from scratch.

Preserve:

- The existing mannequin bust assets and their appearance.
- Mouth-based light-ray emission.
- Independently animated, darting eyes.
- The current ambient environment and background work.
- The existing audio-analysis and modulation architecture.
- The drum bust's current distinctive secondary treatment, unless a specific aspect requires refinement.
- Existing scene registration, controls, and rendering conventions.

The work should concentrate on secondary effects applied to each bust.

Do not replace working features with placeholder implementations. Do not spend the majority of the task reworking the environment, camera, or mouth beams.

First inspect the current implementation to determine exactly which effects each source uses, how their parameters are mapped, and why the bass and synth busts currently look similar.

## 2. Core artistic requirement: three genuinely different visual identities

Each bust needs a different visual behavior, not simply a different color.

Differentiate effects across these dimensions:

1. **Geometry:** What happens to the shape and silhouette of the bust?
2. **Surface:** Does the material ripple, fracture, smear, dissolve, or change appearance?
3. **Spatial presence:** Does the bust leave trails, produce ghost images, grow appendages, or emit surrounding disturbances?
4. **Temporal behavior:** Is the effect continuous, transient-driven, sustained, granular, or intermittent?
5. **Visual texture:** Does it look mechanical, liquid, optical, fragmented, or unstable?

The same effect family may be available to all sources, but default configurations must create visibly different results.

A source should be recognizable from its secondary treatment even when the mouth beams are temporarily disabled and the bust colors are neutralized.

Do not consider changing hue, brightness, or overall effect intensity sufficient differentiation.

## 3. Secondary-effect concepts to investigate

Treat the following as an effect library to explore. These are candidate visual directions, not a requirement to implement every effect.

Select the strongest feasible options based on existing engine capabilities and actual rendered results.

### A. Spectral afterimages

Create multiple slightly displaced copies of the bust, producing a ghosted image that separates and recombines.

Possible behavior:

- Low-frequency energy increases separation between copies.
- Strong transients create a brief burst of separation.
- Sustained energy leaves a persistent but restrained spectral offset.
- Different copies may have slightly different emission or surface coloration.
- The copies fade and converge rather than disappearing abruptly.

This should feel like a physical or optical instability, not merely a conventional motion-blur trail.

Implementation preference: repeated draws of the same asset with controlled transforms and materials, or an existing equivalent.

### B. Liquid undulation

The bust appears to behave like soft, unstable material.

Possible behavior:

- Broad ripples travel across the face and neck.
- The jaw, cheeks, forehead, or neck undergo subtly different deformations.
- Distortion follows a coherent direction or wave pattern.
- Strong audio produces temporary increases in deformation.
- The base silhouette remains recognizable.

Avoid uniform scaling or simple sinusoidal bouncing. The effect should visibly travel across the object.

Implementation preference: existing vertex deformation, material shaders, or another inexpensive local technique. Do not introduce a general-purpose simulation system.

### C. Faceted fragmentation

The bust appears to break into geometric facets or plates while remaining approximately head-shaped.

Possible behavior:

- Small regions of the surface shift or rotate relative to neighboring regions.
- Transients trigger brief displacements.
- Sustained energy creates subtle misalignment.
- Fragments return to their original positions.
- Some regions may briefly appear darker, brighter, or more emissive.

This should resemble an unstable sculpture rather than a generic explosion.

Prefer shader-driven or otherwise inexpensive deformation. Do not rebuild the mesh every frame or introduce large numbers of separate fragment objects unless necessary.

### D. Analog signal corruption

Treat the bust as a damaged video signal.

Possible behavior:

- Horizontal bands shift sideways.
- Facial regions become briefly misaligned.
- Color channels separate spatially.
- Short signal tears appear on transients.
- The face briefly duplicates or snaps to an offset before recovering.

This effect must be visibly localized to the bust whenever feasible. Do not rely solely on full-screen post-processing.

Avoid leaving permanent, uniform scan lines as the only effect. The distortion needs meaningful movement and variation.

### E. Chromatic displacement

Color behaves as though it has become detached from the surface.

Possible behavior:

- Red, magenta, blue, or violet components drift away from the bust's silhouette.
- Different color layers separate by different amounts.
- The separation expands briefly during strong events and contracts afterward.
- Color displacement can affect selected facial regions more strongly than others.

The effect should resemble a spatially unstable optical image, not just a static chromatic-aberration filter.

### F. Surface inversion

Brief regions of the bust change material character or apparent surface orientation.

Possible behavior:

- Selected regions darken toward black or develop metallic highlights.
- Reflective or emissive patches move across the face.
- Portions of the surface appear to invert between light and dark.
- The effect can propagate across the bust in waves.

Use existing material or shader capabilities. Avoid adding expensive reflection systems solely for this effect.

### G. Expanding pressure waves

The bust generates visible disturbances in the surrounding space.

Possible behavior:

- Distorted rings, ripples, or curved wavefronts originate near the head or neck.
- The waves travel outward at controlled speeds.
- Their size, thickness, and intensity respond to source energy.
- Some waves briefly distort background lines or nearby visual elements.
- Strong transients produce more pronounced wavefronts.

Avoid generic concentric audio-visualizer circles. Make the wavefronts irregular, spatial, and integrated into the existing composition.

### H. Dimensional splitting

Parts of the bust appear to occupy slightly different spatial positions or visual states.

Possible behavior:

- The face separates into two or three offset silhouettes.
- The neck and head can have different offset directions.
- The copies may briefly disagree in orientation.
- The image snaps back into alignment after a transient.

The effect should be distinct from ordinary spectral afterimages through its more abrupt, discontinuous behavior.

### I. Surface scanning

A moving band of energy travels over the bust.

Possible behavior:

- A bright or dark band sweeps from forehead to chin, or around the head.
- Surface material and emission change within the band.
- The band accelerates or widens according to musical activity.
- Different source configurations use different sweep directions and temporal patterns.

Keep the effect sculptural and spatial rather than presenting a flat HUD-style scanning line.

### J. Granular disintegration

Small portions of the bust briefly break into drifting points or tiny fragments before returning.

Possible behavior:

- Localized regions dissolve rather than the entire head disappearing.
- Particle activity increases during stronger transients.
- Fragments drift along a coherent direction.
- The original surface reconstructs quickly enough to preserve the identity of the bust.

Prefer an existing GPU particle or instancing system if available. Keep particle counts controlled and avoid CPU-side per-particle processing where it would be expensive.

### K. Mechanical stutter

The bust appears to skip between several nearby spatial or rotational states.

Possible behavior:

- Short discontinuous positional offsets.
- Brief orientation snaps.
- Repeated image positions with different opacities.
- Irregular bursts separated by quiet intervals.

Avoid continuous shaking. The visual character should be discrete and intentionally unstable.

### L. Emissive surface veins

Thin lines or branching patterns appear across the bust.

Possible behavior:

- Lines brighten and fade in response to sustained energy.
- Strong transients activate branches briefly.
- The pattern spreads over portions of the head and neck.
- The effect uses a restrained palette and does not obscure the face.

Implement only if an existing procedural material, texture, or geometry technique can support it economically. Do not build an elaborate procedural-vein generator for this milestone.

## 4. Create source-specific visual personalities

The current scene uses drums, synth, and bass. These should remain useful reference sources during development, but they must not be hardcoded as the only supported source types.

Use the following as initial artistic directions.

### Drum-like source: discrete impact and fragmentation

Preserve the current drum bust's strongest visual traits.

Its secondary treatment should emphasize:

- Abrupt, transient-driven changes.
- Facet displacement or mechanical stutter.
- Short bursts of localized signal tearing.
- Sharp recovery after each hit.
- Strong contrast between quiet intervals and impacts.

The result should feel percussive, fragmented, and discontinuous.

Do not replace the existing treatment with a generic smooth pulse.

### Bass-like source: physical instability

Make the bass bust feel fundamentally different from the drum bust.

Preferred effect combination:

- Liquid undulation or spatially coherent deformation.
- Slow spectral separation.
- Broad pressure waves.
- Occasional larger deformations on strong low-frequency events.
- A slower recovery that gives the object a sense of physical mass.

Its motion should feel continuous, heavy, elastic, and spatially substantial rather than fragmented into individual impacts.

Avoid merely scaling the head up and down with bass amplitude.

### Synth-like source: optical and dimensional instability

Make the synth bust feel distinct from both drums and bass.

Preferred effect combination:

- Chromatic displacement.
- Spectral or dimensional splitting.
- Surface scanning or moving material inversion.
- Slowly evolving emissive patterns.
- Sustained behavior that changes across a note or musical phrase.

Its visual character should feel optical, electronic, and slightly unreal, with color and spatial perception appearing to detach from the bust.

Avoid using the same continuous geometric vibration or horizontal glitch effect as the drum and bass configurations.

### Important qualification

These are initial design hypotheses, not restrictions on the agent's artistic judgment.

If the existing drum treatment is exceptionally successful, preserve it and explore more distinctive alternatives for the other sources.

The result matters more than adhering rigidly to these labels.

## 5. Make effect configurations genuinely different

Implement source-specific effect configurations using existing scene and effect systems.

Each configuration should define:

- Primary effect family.
- Secondary supporting effect.
- Transient response.
- Sustained-energy response.
- Recovery behavior.
- Spatial scale.
- Visual intensity.
- Optional palette configuration.

The same effect may be shared between sources, but its combination and behavior should differ.

Do not create three configurations that differ only in their numerical parameters.

Avoid running every effect simultaneously on every bust. Too many simultaneous effects will erase the visual distinctions this task is intended to create.

Choose a small number of complementary effects per source and make them work together.

## 6. Dynamic support for one to four sound sources

Change the scene so the number of busts is determined by the number of assigned sound sources, from one to four inclusive.

Requirements:

- One assigned source produces one bust.
- Two assigned sources produce two busts.
- Three assigned sources produce three busts.
- Four assigned sources produce four busts.
- Removing a source removes its bust cleanly.
- Adding a source creates the corresponding bust without requiring scene reconstruction or a manual layout edit.
- Each bust receives its own source assignment, secondary-effect configuration, and parameter state.
- No fifth bust is created.
- A missing or invalid source must not create a phantom bust.
- Zero assigned sources should be handled gracefully using the existing scene lifecycle conventions, without a crash or stale audio response.

Do not hardcode the scene to exactly three busts or hardcode drum, synth, and bass as the only possible inputs.

Inspect how AV Gen currently represents assigned sound sources, stems, channels, and source types. Reuse the existing abstraction rather than creating a second source registry.

Distinguish between a source's **identity** and its **visual treatment**.

A source may be a named stem, an audio channel, a bus, or another supported audio source. Do not assume every source is one of the three example instruments.

### Source identity and effect assignment

Prefer a stable source identifier when the engine provides one.

Each bust's configuration should be derived from the assigned source and an explicit effect-profile selection or deterministic default.

If the source is a known drum, bass, or synth stem, the initial profiles described above are suitable defaults.

For other source types, use an existing source classification or the current general-purpose audio signals. Do not invent new instrument classifiers.

If source metadata is unavailable, select a sensible general-purpose profile based on supported signals and allow the effect profile to be configured through existing scene controls.

Do not silently assign every unknown source the same visually generic treatment if a more useful profile can be selected.

### Stable configuration across source changes

When sources are added, removed, or reordered:

- Preserve effect state for sources that remain assigned, where practical.
- Avoid unnecessarily resetting the remaining busts.
- Clean up removed sources and their effect state.
- Ensure audio signals do not leak between busts.
- Avoid duplicate entities after repeated reconfiguration.

Use the existing entity lifecycle and parameter ownership conventions.

## 7. Composition for variable bust counts

The scene must compose itself correctly with one, two, three, or four busts.

Do not simply append more busts beyond the existing layout.

Provide deliberate layouts for each count:

- **One bust:** a large hero composition with generous negative space.
- **Two busts:** a balanced but slightly asymmetric composition.
- **Three busts:** a readable triangular or gently staggered composition.
- **Four busts:** a balanced spatial arrangement with enough separation to distinguish each bust and its effects.

Prefer a small number of explicit layout rules over arbitrary runtime placement.

Busts should retain sufficient space for their secondary effects and mouth beams.

The four-bust arrangement must not reduce every bust to an unrecognizable thumbnail. Adjust scale and depth thoughtfully.

If the existing camera and spatial layout support depth, use it to help differentiate entities without hiding any bust behind another.

The scene should remain a unified composition rather than four isolated visualizer panels.

## 8. Audio mapping and timing

Use the existing AV Gen audio-analysis and modulation pipeline.

Do not build a new analyzer or reintroduce retired inferred percussion identities.

Use available signals for:

- Low-frequency energy.
- Mid-frequency energy.
- High-frequency energy.
- Broadband amplitude.
- Transient intensity.
- Sustained energy.
- Existing onset or envelope information where appropriate.

Choose mappings appropriate to the effect family.

Examples:

- Liquid deformation follows smoothed low-frequency energy.
- Fragment displacement responds to transient strength.
- Chromatic displacement follows sustained tonal energy and selected transients.
- Spectral separation grows with signal intensity and decays naturally.
- Pressure waves are triggered by strong events and continue traveling after the initial event.

Avoid forcing every effect to react directly to raw amplitude. That will make all busts move together and recreate the original problem.

Use appropriate smoothing, attack, decay, and event-triggered behavior.

Validate actual response against a real audio passage. Do not accept mappings merely because their parameters change in a debugger.

## 9. Rendering performance

Maintain the established 1080p/30 FPS live target.

Before implementing candidate effects, inspect existing rendering support and measure the current scene's cost.

Prefer:

- Repeated draws of existing geometry for ghosting.
- Existing shader or vertex-deformation techniques.
- Existing material modulation.
- Instancing for small fragments where appropriate.
- Controlled particle counts.
- Localized effects instead of full-screen processing.

Avoid:

- Per-frame CPU reconstruction of complex meshes.
- Large numbers of transparent overlapping surfaces.
- Expensive volumetric simulation.
- Unnecessary real-time shadows.
- New render passes without clear visual benefit.
- General-purpose effect frameworks built solely for this scene.

Profile the scene with one, two, three, and four busts where practical.

Record CPU time, GPU time, resolution, and frame rate. Identify any effect that scales poorly with entity count and simplify it without destroying its visual character.

## 10. Artistic validation is mandatory

Do not stop after implementing the effect configurations.

Capture and inspect actual rendered results.

Create comparisons showing:

1. The current drum treatment.
2. The proposed bass treatment.
3. The proposed synth treatment.
4. All three running simultaneously.
5. A fourth, different source with its assigned profile.
6. One-bust and four-bust layouts.
7. Quiet and high-energy passages.

Review the results with mouth beams temporarily hidden or visually neutralized to determine whether the secondary effects are independently distinctive.

Then restore the normal presentation and ensure the secondary effects complement, rather than compete with, the mouth beams and animated eyes.

If the bass and synth busts still look similar, revise their effect combinations before adding more effect families.

A successful result should be recognizable from geometry, motion, and surface behavior even when the palette is neutralized.

## 11. Implementation workflow

1. Audit the existing secondary effects and their actual visual output.
2. Identify why the current bass and synth treatments converge visually.
3. Inspect reusable effect implementations and source-assignment APIs.
4. Produce a short plan identifying the strongest candidate effects.
5. Preserve the drum treatment as a baseline.
6. Implement and visually validate a distinct bass profile.
7. Implement and visually validate a distinct synth profile.
8. Implement a fourth-source fallback or configurable profile.
9. Generalize the entity lifecycle and layout for one to four assigned sources.
10. Build, run tests, capture comparisons, and profile performance.
11. Fix visual shortcomings discovered during inspection.
12. Audit documentation and the final diff for unrelated changes.

Use two parallel subagents where supported:

- **Art and effect subagent:** investigate effect combinations, existing implementations, and ways to make the bass and synth treatments visually distinct.
- **Source lifecycle subagent:** inspect dynamic source assignment, entity ownership, and variable-count composition.

Integrate findings before editing shared files. Do not let parallel work produce conflicting scene implementations.

Avoid unnecessary architectural changes, unrelated renderer work, and new compatibility layers.

## 12. Acceptance criteria

The task is complete when:

- The existing mouth beams and animated eyes remain functional.
- The drum treatment retains its distinctive character.
- Bass and synth have clearly different secondary-effect identities.
- The differences remain obvious with neutralized colors and hidden mouth beams.
- The effects respond meaningfully to their assigned sources.
- The scene supports one, two, three, or four assigned sources dynamically.
- Adding and removing sources does not produce stale entities, duplicate busts, or cross-source signal leakage.
- Unknown source types receive a sensible configurable treatment.
- The composition works for all supported source counts.
- The scene remains within its measured live-rendering budget.
- Captured output demonstrates the improvements.

The desired result is a collection of strange entities, each expressing sound through a different physical or perceptual phenomenon — fragmentation, liquid deformation, optical splitting, signal corruption, or other carefully selected behaviors.

**Do not deliver a scene in which all busts still perform the same generic vibration or glitch effect with different colors. The secondary effects must make the sources feel fundamentally different.**
