# Aurora Borealis pass 2: the owner's brief (verbatim, 2026-10-24)

The owner's covering note: "encourage deep research on any of this if needed first".

Coordinator notes:
- The stems are `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav` (not `audio/feline-footwear-stems`).
- Starting point: scene/aurora-borealis 20dcd915 (stop 2, not yet merged; see PROGRESS.md and the stop-2 final report).
- Engine kept from Living Prism and pass 1: ADR-1600..1643, 1650 (sky mirror), 1651 (ribbon plane ladder), 1652 (ribbon pulse), 1660 (ribbonCoverage).
- The brief says not to rely on automatic quality reductions to hide a costly design: measure at Ultra; the ladder is the safety net, not the plan.

---

# AV Gen — Aurora Borealis: Landscape, Lighting, Water & Celestial VFX Polish

## Mission

Continue development of the existing Aurora Borealis scene in `av-gen`.

The scene already has a strong visual foundation: a purple/pink/blue aurora, dark faceted mountain ranges, a winding reflective river, a starry night sky, and audio-responsive aurora movement.

We are not starting over. We are refining the existing scene into a more convincing, visually rich, musically expressive live VFX environment.

The main improvements are:

1. Rework the landscape composition into a wide, deep mountain valley with a river that snakes naturally toward distant mountains.
2. Eliminate the visually awkward raised riverbank and make the mountain terrain meet the water naturally.
3. Add selective, musical, luminous edge treatments to the faceted mountain geometry.
4. Make the water visibly flow while preserving the existing attractive reflections.
5. Experiment with musical light pulses traveling through the river and illuminating nearby geometry.
6. Experiment with a more elaborate celestial sky while retaining the current sky as an immediately available fallback.
7. Add occasional, visually spectacular comets that can illuminate the scene.
8. Profile and validate every change against the existing 1080p, 30 FPS live-rendering target.

The goal is a polished audiovisual scene, not a collection of disconnected effects. All new effects must support the landscape composition and work together as one coherent visual experience.

## 1. Mandatory preflight: inspect before modifying

Before making production changes, inspect the current implementation and identify the exact scene files, assets, shaders, configuration, and relevant engine systems.

Investigate:

- The current Aurora Borealis scene and its registration.
- The existing landscape mesh generation and terrain materials.
- The river geometry, water shader, reflection implementation, and current flow animation.
- The aurora implementation and its current controls.
- The skybox or sky-rendering system.
- The existing audio-reactivity and stem-analysis architecture.
- Existing material, lighting, bloom, reflection, and post-processing capabilities.
- The current profiling and live-mode performance tools.
- Useful changes retained from Living Prism, including any signal-timing improvements.
- The actual Feline Footwear stems and their current synchronization and analysis path.

Use two parallel subagents for bounded investigations:

**Subagent A — Landscape and rendering:** inspect the scene composition, geometry, materials, water, reflections, sky, and existing rendering costs.

**Subagent B — Musical reactivity and performance:** inspect stem signals, timing, parameter routing, current audio-responsive behaviors, and the profiling workflow.

Synthesize their findings into a short implementation plan before modifying production code. Then proceed with implementation rather than stopping after research.

Record a baseline of the existing scene at the intended live resolution. Capture representative screenshots or clips and CPU/GPU timings if the current tools support them.

Use the actual repository and rendered output as the source of truth. Do not assume a feature exists or behaves as expected merely because it is described in documentation.

## 2. Hard constraints

### Live performance

The target is sustained **30 FPS at 1920 × 1080** on the established M2 Max / Apple Silicon Metal-Dawn development baseline.

The 33.3 ms frame interval is the total engine budget, not an allowance for the scene to consume the entire interval.

- Measure CPU and GPU frame times separately where possible.
- Profile representative aurora peaks and combined effect states.
- Avoid unbounded overdraw, unnecessary transparent layers, excessive draw calls, costly full-screen passes, and per-frame CPU geometry regeneration.
- Prefer cached geometry, persistent resources, and inexpensive shader-driven animation.
- Evaluate the cumulative cost of all effects, not just each effect in isolation.
- Preserve meaningful headroom for the rest of the engine.
- Do not claim success based solely on average FPS or a single screenshot.

If a proposed effect cannot meet the budget, simplify it or provide a cheaper implementation. Do not introduce automatic quality reductions that conceal an unnecessarily expensive design.

### Audio and stems

Continue using the existing Feline Footwear development set in `audio/feline-footwear-stems`:

- Drums
- Bass
- Synth

Verify the actual filenames and existing analysis signals.

The stems are synchronized analysis sources. Preserve the intended stereo playback workflow and existing stem-aware signal architecture.

Do not replace the stems, invent synthetic audio inputs, or build a separate scene-specific analysis pipeline.

### Scope

- Reuse existing rendering and modulation capabilities.
- Preserve useful engine changes already made.
- No renderer rewrite.
- No new dependencies unless genuinely necessary.
- No expensive effect is mandatory merely because it is visually interesting.
- No backwards-compatibility shims for discarded scene experiments.
- Keep the work focused on Aurora Borealis.

## 3. Priority One: Rebuild the landscape composition

### Current problem

The current landscape has attractive facets and colors, but the river feels like a channel cut through a raised, broad foreground bank. The composition lacks a sufficiently strong sense of a wide mountain valley receding into the distance.

The existing winding river is worth preserving as a concept. The problem is its relationship to the surrounding terrain and the camera's field of view.

### Target composition

Reframe the scene as a wide-angle view looking down a substantial mountain valley.

The viewer should immediately perceive three layers:

**Foreground**

- Mountain slopes and rocky terrain occupy both sides of the frame.
- The river enters the foreground or lower-middle frame with enough width to establish its reflective surface.
- The terrain slopes naturally toward the water.
- There should be no broad, raised, flat, ribbon-like bank separating the river from the mountains.
- The nearest terrain establishes scale and provides large, readable facets.

**Middle distance**

- The valley opens enough to reveal the river's path.
- Mountain ranges frame both sides of the river.
- The river bends in a few deliberate, broad curves.
- The terrain should have distinct left and right silhouettes rather than mirrored or repetitive mountain shapes.
- The river should remain visible through the major bends.

**Background**

- The river narrows and disappears into progressively smaller mountain ranges.
- Atmospheric perspective reduces the contrast and saturation of distant peaks.
- The horizon provides a strong visual anchor beneath the aurora.
- The landscape should communicate substantial depth rather than looking like a flat collection of layered triangles.

### Geometry requirements

Inspect the existing terrain and river geometry before deciding whether to modify the mesh generation, river path, camera, or all three.

The river should be integrated into the landscape.

- The water surface must meet the terrain at a clean shoreline.
- Mountain slopes and valley floors should descend toward the water.
- Avoid visible gaps, raised lips, floating terrain, intersecting bank strips, or abrupt changes in elevation at the shoreline.
- The riverbed and terrain should be shaped around the water's path where necessary.
- Keep the river width, shoreline curvature, and terrain height consistent.
- Ensure the river remains visible along its full path into the distance.

Use a shared river path or another coherent representation of the water corridor and adjacent terrain if that simplifies alignment. Do not introduce an unnecessarily generalized terrain framework.

The terrain may use simplified meshes and deliberate faceting. It does not need geological realism.

### Camera and framing

Experiment with the camera's field of view, position, height, and pitch to achieve the intended wide-angle composition.

The result should establish foreground, middle distance, and background without excessive distortion.

The camera should continue to provide a suitable slow journey through the environment. Do not sacrifice the valley's composition for speed or arbitrary camera choreography.

Evaluate the scene from the actual live camera and throughout its intended movement.

### Acceptance criteria

- Both sides of the valley clearly frame the river.
- The river visibly snakes toward distant mountains.
- The foreground is more open and compositionally intentional.
- Mountains and slopes reach the water naturally.
- The current raised riverbank appearance is eliminated.
- The landscape remains attractive independently of the aurora's brightness.
- The revised geometry does not create a material performance regression.

## 4. Priority Two: Musical light along mountain edges

### Artistic intent

The faceted mountain geometry already provides strong planar silhouettes. Use that geometry to create an additional layer of luminous, music-responsive detail.

Selected edges where mountain planes meet should catch and emit light, creating the impression that the aurora is energizing the landscape.

The result should feel like light tracing through a crystalline or mineral landscape, not like every triangle in a low-poly model has been outlined with a neon wireframe.

### Initial implementation

First determine how the existing meshes represent facets, normals, topology, and material boundaries.

Choose the least expensive appropriate technique. Candidates include:

- A material or shader treatment that emphasizes suitable facet boundaries.
- A lightweight edge mask derived from mesh geometry.
- A selectively generated edge mesh or line representation.
- Existing emissive material and bloom capabilities.

Do not implement all of these. Select one approach based on the current renderer and actual visual quality.

Avoid adding per-frame CPU geometry generation.

### Edge selection and appearance

Not every edge should be equally bright.

Prioritize:

- Major ridgelines.
- Selected intersections between prominent facets.
- Edges facing the aurora or the camera.
- A few foreground and midground contours that establish depth.

Distant terrain should receive less detail and lower contrast.

The edges should be fine enough to preserve the faceted material surfaces. Avoid thick, continuous outlines that obscure the geometry.

Use colors derived from the current aurora palette: violet, lavender, magenta, electric blue, and pale pink.

Edge light should complement the existing surface colors rather than replacing them.

### Musical behavior

Use the existing stem signals to control distinct aspects of the effect.

**Bass**

- Controls the broad intensity of the edge illumination.
- Can produce waves of brightness that travel across selected ridge sections.
- Should create a sense of energy moving through the terrain without requiring vertex deformation.

**Drums**

- Kick: brief, broader pulses of edge brightness.
- Snare: sharper accents on selected ridgelines or facet intersections.
- Hi-hat: restrained, selective sparkling on a small number of edges, only if the existing detector quality supports it.

Avoid having every detected event illuminate every edge.

**Synth**

- Influences the edge palette and the overall luminous atmosphere.
- Can gradually shift the edge color balance in response to the musical context.

These are starting mappings. Tune them against actual Feline Footwear playback.

Use existing stem-specific signals and the current modulation architecture. Verify detector timing and avoid unnecessary signal latency.

### Bloom

Use the existing bloom or glow path if available and affordable.

The edges should appear luminous, but the underlying mountain facets must remain legible.

Avoid overexposure, excessive pink wash, or bloom that merges neighboring edges into broad, shapeless areas.

### Acceptance criteria

- The mountain facets remain visually readable.
- Selected edges emit colored light and have a convincing relationship to the aurora.
- Musical accents produce clear but controlled changes.
- Edge lighting varies by depth and importance.
- The effect does not resemble a uniform neon wireframe.
- Its cost is measured in the complete live scene.

## 5. Priority Three: Make the river visibly flow

### Artistic intent

The current mirror-like surface is attractive and should not be discarded.

The missing quality is the sense of flowing water associated with a long-exposure photograph: elongated, soft streaks of light and a visible directional flow while retaining a coherent reflection of the sky and mountains.

The river should look like a moving body of water, not a perfectly static mirror or a noisy, turbulent surface.

### Implementation strategy

Inspect the current water shader and reflection system first.

Prefer layered, low-cost animation over a full fluid simulation.

Evaluate the following components:

1. Slow directional flow along the river.
2. A second, weaker normal or distortion layer moving at a different scale.
3. Elongated highlights that follow the water's direction.
4. Gentle, spatially varying distortion of reflected aurora light.
5. A small amount of fine surface variation to reveal motion in darker regions.

The water should retain relatively stable reflections of the mountain silhouettes and major aurora shapes.

Avoid uniform high-frequency noise, large waves, excessive ripples, or distortion that destroys the mirror-like quality.

### Long-exposure visual character

Use elongated, soft bands of light rather than tiny glittering highlights everywhere.

These bands should move in the river's flow direction and react to the aurora's color.

In brighter areas, the water may contain long pink, violet, and blue streaks. In darker areas, preserve the mountain reflections and subtle surface movement.

The exact implementation can use the existing water material, shader-driven normal motion, or a similarly inexpensive approach supported by the renderer.

Do not require a costly new reflection technique just to achieve visible motion.

### Musical behavior

- Bass may subtly increase flow distortion or the strength of elongated highlights.
- Synth may influence the color and persistence of reflected light.
- Drum transients may create a short-lived disturbance or bright ripple in the water.
- The overall motion must continue smoothly when the music is quiet.

Avoid a direct one-to-one mapping in which every drum hit generates a large circular ripple.

### Acceptance criteria

- The water visibly moves even during a quiet passage.
- The surface retains attractive reflections.
- Directional highlights suggest long-exposure flow.
- Aurora reflections change coherently with the aurora.
- The river does not look like boiling water or a turbulent ocean.
- The effect works along the winding river without obvious texture repetition or discontinuities.

## 6. Priority Four: Traveling light pulses beneath the river surface

### Artistic intent

Experiment with light moving through the river corridor, as though luminous energy is traveling beneath the water and refracting through the surrounding environment.

This should become a distinctive audiovisual behavior: a wave of colored light travels along the river, illuminating the water surface, riverbed, and nearby mountain or shoreline facets.

The effect should feel integrated with the landscape rather than like a glowing tube placed inside the river.

### Preferred implementation

Start with a cheap, controllable approximation.

Represent the river's path using the existing water geometry, spline, centerline, or another appropriate existing representation.

Use that representation to define a moving light band or luminous field that progresses along the river.

The implementation may use:

- A shader-driven emissive band parameterized along the river path.
- A small number of inexpensive localized light contributions, if the renderer supports them efficiently.
- A restrained surface-lighting approximation that colors the water and adjacent terrain.
- A combination of these methods, provided the additional cost remains controlled.

Prefer a shader-driven solution if actual dynamic lighting would require too many light objects or expensive per-frame updates.

Do not create a complex underwater simulation, volumetric scattering system, or full real-time caustics implementation.

### Visual behavior

A pulse should have:

- A clear point of origin or leading edge.
- A direction of travel following the river.
- A soft luminous envelope.
- A controlled duration and decay.
- A coherent relationship between its color and the current aurora palette.
- A visible reflection or refraction-like response at the water surface.

The pulse may briefly illuminate nearby terrain where the river approaches the shoreline.

The riverbed can remain simple. It only needs enough geometry or shading variation to make the traveling light legible.

Avoid requiring elaborate underwater rocks or detailed submerged environments.

### Musical triggers

Use the existing stem signals.

Potential mappings:

- Kick: launches a prominent pulse along the river.
- Snare: generates a shorter, sharper pulse or a brief secondary accent.
- Bass: influences the pulse's width, energy, or propagation speed.
- Synth: influences its color and persistence.

Do not trigger a new full-length pulse on every hi-hat or minor onset.

Use cooldowns, envelope decay, or another existing mechanism to prevent overlapping pulses from turning the river into a permanently overexposed strip.

Where multiple pulses overlap, combine their effects or cap their contributions rather than allowing unbounded work.

### Acceptance criteria

- A pulse can be seen traveling along the river.
- The pulse follows the actual winding path.
- The water and nearby terrain receive a coordinated lighting response.
- The effect is visually legible against the baseline reflection.
- Pulses feel musically timed and have controlled decay.
- Multiple triggers cannot cause uncontrolled accumulation or excessive brightness.
- The implementation remains inexpensive in the full scene.

If a believable beneath-the-surface appearance proves too costly, retain the best-performing surface-based approximation. The visual impression matters more than physically complete light transport.

## 7. Priority Five: Reversible celestial sky experiment

### Artistic intent

Experiment with an additional celestial layer that could make the sky feel larger and more otherworldly.

Possible elements include:

- A faint Milky Way or galactic dust band.
- Subtle nebular color variation.
- One or two distant planetary silhouettes.
- Very restrained celestial highlights or atmospheric halos.

This is explicitly an experiment, not a requirement to replace the current sky.

The current starry twilight sky and aurora already work. The new layer must earn its place.

### Preserve the current sky

Keep the current sky available as a clean, immediately selectable fallback.

Implement the experiment as a separate sky variant, configurable layer, or equivalent existing scene setting.

The default and fallback must not require deleting assets, reverting source changes, or rebuilding a different version of the scene.

Make it easy to compare:

- Existing sky.
- Existing sky plus subtle Milky Way.
- Existing sky plus a restrained planetary element.
- Combined celestial treatment, only if it proves successful.

Reuse the existing sky-rendering architecture.

### Visual constraints

The aurora remains the primary focal point.

The Milky Way, if used, should have a broad but subtle form with restrained contrast. It should not introduce another dominant bright ribbon competing with the aurora.

Planets should be distant, relatively small, and artistically integrated into the composition.

Avoid enormous foreground planets, excessive nebula saturation, dense star fields, generic space wallpaper, or a busy science-fiction sky.

The result should feel like a convincing artistic interpretation of a cosmic night sky viewed from a mountain valley.

### Musical behavior

Keep the celestial background mostly stable.

Potential responses include:

- Very subtle palette shifts tied to the broader musical context.
- A small amount of slow celestial movement.
- Brief increases in luminosity associated with significant musical events, if they look intentional.

Do not make the Milky Way pulse on every beat or compete with the aurora's primary musical responses.

### Performance and fallback

Prefer a static sky texture, procedural sky shader, or another inexpensive existing technique.

Avoid expensive volumetric nebula rendering, elaborate parallax systems, or multiple high-cost sky passes.

If the experiment degrades composition or performance, disable it and retain the existing sky.

### Acceptance criteria

- The new sky can be compared directly with the existing version.
- The aurora remains visually dominant.
- The celestial layer adds scale and depth rather than clutter.
- The original sky remains intact and immediately selectable.
- Performance cost is measured with the new layer enabled.
- The experiment may be rejected if it does not improve the scene.

## 8. Priority Six: Occasional musical comets

### Artistic intent

Introduce rare, elegant comet events that cross the night sky and briefly illuminate the surrounding landscape.

A comet should feel like a special event in the performance, not a permanent decorative object.

It may contain:

- A bright but controlled head.
- A tapered luminous tail.
- A subtle trail that dissipates over time.
- A brief aurora response.
- A corresponding color wash or highlight on the river and nearby terrain.

Keep the comet visually integrated with the existing palette.

### Implementation

Use a small, bounded effect system.

- Reuse existing particle, ribbon, line, or shader capabilities where suitable.
- Use a deterministic trajectory with a controlled duration.
- Animate the tail using inexpensive shader or geometry techniques.
- Avoid heavy particle counts and expensive volumetric trails.
- Do not allocate or rebuild large geometry every frame.
- Ensure that comet events can be disabled independently.

A small number of carefully designed events is preferable to a large particle system.

### Musical triggering

Comets should be rare.

Potential triggers include:

- A significant musical transition.
- A strong phrase-level accent.
- A deliberately selected event in the existing stem-driven control architecture.

Do not trigger comets on every kick or snare.

Use a cooldown and bounded event state so repeated high-energy passages cannot create a continuous stream of comets.

The trigger should be deterministic or reproducible enough for testing against Feline Footwear.

### Lighting response

A comet may briefly:

- Increase the brightness of the nearby aurora.
- Create a traveling highlight on the river.
- Add a restrained color accent to selected mountain edges.
- Produce a subtle environmental light wash.

Use existing shader parameters and lighting systems where possible.

Do not introduce a new dynamic-lighting framework solely for comets. If a real moving light is too expensive, use a screen-space, material, or shader-based approximation.

### Acceptance criteria

- Comets are visually polished and infrequent.
- Their trails are smooth and do not produce distracting artifacts.
- The aurora remains the dominant visual feature.
- Events create a visible but restrained environmental response.
- The system has a hard bound on simultaneous events.
- Comets can be disabled without affecting the baseline scene.

## 9. Additional polish: unify the effects

After the primary work is complete, inspect the full composition for opportunities to improve the scene without adding unnecessary systems.

Potential refinements:

- Subtle atmospheric haze that improves separation between mountain ranges.
- A restrained horizon glow that reinforces the aurora's light.
- Improved tonal separation between foreground terrain and the river.
- A few carefully selected facet highlights that help establish the mountain geometry.
- Smooth palette transitions that coordinate the aurora, edge lighting, water, and river pulses.
- Better variation in aurora movement so the entire sky does not move as a single rigid sheet.
- A small amount of subtle reflected light variation on the river to reinforce depth.

These are optional and must be justified by the rendered result.

Avoid adding decorative vegetation, floating objects, random particles, unnecessary architecture, or more layers simply to make the scene busier.

The scene should become richer through coherent lighting, color, composition, and musical behavior rather than visual clutter.

## 10. Stem-driven behavior: preserve a unified control model

All new musical effects should fit into one coherent model.

### Broad musical context

Use the existing stem-aware analysis to derive stable controls for:

- Overall energy.
- Palette selection and blending.
- Aurora brightness and broad movement.
- Mountain-edge intensity.
- River reflection intensity.
- Water-flow strength.

Do not assume all these controls require separate new analysis signals. Reuse existing signals wherever they provide suitable behavior.

### Transient events

Use existing drum-specific signals for:

- Aurora accents.
- Selective mountain-edge pulses.
- Traveling river-light events.
- Rare, stronger visual events when appropriate.

Keep transient responses distinct from slower continuous modulation.

### Avoid overreaction

- Do not allow every effect to respond to every stem simultaneously.
- Avoid stacking multiple smoothing stages that add latency.
- Avoid excessive bloom and repeated flashes.
- Use sensible attack, release, decay, and cooldown behavior.
- Preserve the visual composition during quiet sections.
- Verify that stem signals actually trigger as expected.

The objective is musical coherence, not maximum parameter activity.

## 11. Implementation order and gates

Implement the work in the following order.

### Gate A — Landscape composition

Complete the wider valley, corrected shoreline, natural terrain-to-water transitions, and improved camera framing.

Capture before-and-after screenshots from the same representative camera position.

Do not proceed until the riverbank issue is resolved and the three depth layers are clearly readable.

### Gate B — Mountain-edge lighting

Implement and tune the edge effect with the existing aurora and terrain.

Verify that the edges look integrated with the mountain facets and do not create uniform neon outlines.

Profile the result.

### Gate C — Moving water

Introduce directional flow, elongated highlights, and restrained reflection distortion.

Verify that the river looks animated without losing its mirror-like character.

Profile the result.

### Gate D — Traveling river light

Implement the initial pulse as an inexpensive, shader-driven or otherwise bounded effect.

Test the pulse manually before connecting it to music. Verify the actual river path and lighting appearance.

Then connect it to suitable stem signals and test the event timing.

Profile the result.

### Gate E — Celestial sky experiment

Create the reversible sky variant and compare it with the baseline.

Evaluate the composition, aurora dominance, and performance. Keep the existing sky as the default unless the experiment clearly improves the scene.

### Gate F — Comets

Add the bounded comet effect and its environmental response.

Test it independently, then connect it to an appropriate rare musical trigger.

Profile the combined effect.

### Gate G — Full-scene integration

Play the actual Feline Footwear stems through representative quiet and energetic passages.

Review the complete scene with all accepted features enabled.

Tune the interaction between the aurora, edge lighting, water, river pulses, and comets.

Measure performance and remove any effect that causes unacceptable frame-time spikes.

Do not allow a later optional feature to destabilize an already accepted scene. If a feature fails its visual or performance gate, disable or simplify that feature and continue.

## 12. Performance validation

Test the full live scene at 1920 × 1080 on the established M2 Max baseline.

Record:

- CPU frame time.
- GPU frame time.
- Sustained FPS.
- Worst observed frame-time spikes.
- The rendering configuration and scene options enabled.
- The musical passage used.
- Whether all accepted effects were active during the measurement.

Capture measurements at these stages:

1. Existing baseline.
2. Revised landscape.
3. Landscape plus mountain-edge lighting.
4. Landscape plus moving water.
5. Landscape plus traveling river pulses.
6. Celestial sky experiment enabled and disabled.
7. Comets enabled and disabled.
8. Final combined configuration.

Use existing profiler and capture tooling.

The 30 FPS target applies to the final combined scene, not just the baseline or individual features.

If the combined scene misses the target, profile and address the actual bottleneck. Do not simply disable arbitrary effects without documenting the tradeoff.

If the target cannot be verified on the intended hardware, explicitly identify what was tested and what remains unverified.

## 13. Final acceptance criteria

The milestone is complete when:

- The river runs naturally through a wide mountain valley.
- The terrain meets the water cleanly without the existing awkward raised bank.
- The foreground, middle distance, and background have a strong visual hierarchy.
- Mountain-edge lighting adds a musical visual layer without obscuring the faceted geometry.
- The river has visible directional movement while retaining attractive reflections.
- Traveling light pulses work along the river path and create a coherent surface and environmental response.
- The celestial sky experiment is reversible, and the existing sky remains available.
- Comets are polished, rare, bounded, and independently configurable.
- New effects use the existing Feline Footwear stem analysis and signal architecture.
- All accepted effects work together as one composition.
- The final configuration is tested against the 1080p/30 FPS target.
- Tests, relevant checks, scene configuration, and documentation are updated as needed.
- No unnecessary engine rewrite, duplicated analysis pipeline, or unbounded visual effect has been introduced.

## 14. Final report

At completion, provide:

1. A summary of the landscape changes.
2. The implementation approach for mountain-edge lighting.
3. The water-flow technique and reflection tradeoffs.
4. The river-pulse implementation and stem mapping.
5. The celestial experiment's outcome and fallback behavior.
6. The comet implementation and trigger rules.
7. Before-and-after screenshots or clips, including the actual combined scene.
8. Measured performance results for the final accepted configuration.
9. Tests and validation results.
10. Any remaining visual, timing, or performance limitations.

Be explicit about which optional effects were accepted, simplified, or rejected.

Do not report an effect as complete or a performance target as achieved without evidence.

## Guiding principle

**Make the existing scene more convincing, more musically expressive, and more visually integrated without making it materially harder to render or maintain.**

Composition comes first. Light should travel coherently through the environment. Music should control the scene through a small number of deliberate visual behaviors. Every additional effect must earn its place through the rendered result and remain within the live-performance budget.
