# AV Gen — Resonant Visual Worlds: Comprehensive Artistic Identity & Rendering Overhaul

(Owner brief, received 2026-10-17, saved verbatim; the owner's first paste was cut off partway through §13, and the owner then sent §13-15 and the final principle separately, appended below. Coordinator notes: the owner has said to ignore the 120 Hz / 8.33 ms reference — the live target is 1080p at 30 fps; live scenes carry no audio file — test with assets/audio/feline-footwear.wav fed on the command line; "Wave Form Dimension" is the scene registered as "Waveform Dimension". The owner's process instruction: research deeply first, think like a professional VFX artist, plan thoroughly up front, and debate the candidate approaches to decide which will look best before building — don't go blindly down one path and evaluate only at the end.)

## Mission

Perform a substantial visual-development pass on three existing AV Gen scenes:

1. **Cymatic World** — a rich, continuously evolving world of complex resonant patterns inspired by cymatic phenomena, interference, modal vibration, and beautifully organized nonlinear geometry.
2. **Resonant Silk** — an immersive, flowing environment of luminous, fluid, silk-like structures with graceful motion, layered depth, sophisticated color, and a sense of continuous space.
3. **Wave Form Dimension** — an expansive, animated world built from traveling waves, intersecting wavefronts, layered interference patterns, and flowing spatial structures.

These scenes already have an underlying audio-reactive resonator concept that has proven useful. Preserve and build upon that system. The objective is not to replace the resonator architecture or add an entirely new framework without justification. The objective is to develop the **artistic systems that transform resonator output into compelling visual worlds**.

The current results have several recurring problems:

- Materials look overly smooth, glossy, and plasticky.
- Bright white specular highlights often dominate the underlying color.
- The backgrounds are frequently black or nearly black, creating the appearance of isolated objects floating in empty space.
- Color variation exists, but it does not yet consistently produce the richness, depth, and visual sophistication of high-quality procedural VFX.
- Cymatic World remains too zoomed-in and relies too heavily on a repeated radial/cellular pattern. The geometry changes, but the underlying pattern identity often stays the same.
- Resonant Silk reads too much like a small number of glossy ribbons or sheets against a void.
- Wave Form Dimension has interesting folded geometry, but its surrounding space is not sufficiently alive or integrated with the main structures.
- Temporal evolution is too frequently expressed as local deformation or color changes rather than changes in the composition, spatial relationships, and overall visual character.

We need to address these at their source.

**This is an artistic systems task, not an effects-stacking task.** Do not attempt to solve weak geometry with bloom, weak composition with more particles, weak materials with more specular intensity, or weak animation with random noise.

The desired result is three distinct, memorable, professional-quality audiovisual worlds that can sustain visual interest during a musical performance.

---

# 1. First: Inspect, Measure, and Understand the Existing Implementation

Before changing code, investigate the actual implementations of all three scenes.

Read the relevant scene definitions, geometry-generation code, shaders, material configuration, lighting setup, post-processing pipeline, audio-reactive bindings, parameter definitions, camera behavior, and any existing resonator infrastructure.

Determine:

- Which parts of the scene are generated procedurally.
- Which parts of the geometry are static and which are dynamically deformed.
- How resonator state, frequencies, amplitudes, phase, damping, and audio analysis currently reach the scene.
- How the scene's palette is represented and interpolated.
- How material parameters are controlled.
- How the camera is positioned and animated.
- Whether the renderer provides environment lighting, procedural sky/background rendering, fog, depth information, reflections, emission, and bloom.
- Whether any existing shared rendering or scene systems can support the requested improvements.
- What the current performance cost is on the supported live-rendering path.

Use the actual architecture discovered in the repository. Do not invent APIs or assume that a particular rendering feature exists.

Inspect recent scene screenshots and any existing before/after render sheets. Use the current output as a baseline, not as a target to preserve.

Produce a short internal implementation plan before coding. Then proceed with implementation rather than stopping after analysis.

## Non-negotiable workflow

For each scene:

1. Capture a baseline at a known camera configuration and reproducible state.
2. Identify the primary visual failure modes.
3. Implement the underlying geometry, composition, material, or lighting improvements.
4. Render the scene using the actual AV Gen renderer.
5. Inspect the result visually.
6. Identify what still looks weak or incorrect.
7. Make another targeted iteration.
8. Compare the final result against the baseline.

A successful build, passing unit tests, or a plausible shader implementation does not establish artistic success.

**Rendered evidence is required.** If a suitable automated screenshot/render harness exists, use it. Otherwise, establish the most reliable available way to render and inspect each scene. Do not claim visual improvements that you have not inspected.

If the environment cannot render a scene, complete the implementation work that can be validated, explain the limitation, and provide a clear description of what remains unverified.

---

# 2. Establish the Artistic Standard

The scenes should feel like authored audiovisual art, not default 3D primitives with audio-reactive parameters.

Aim for a combination of:

- Sophisticated procedural VFX.
- Carefully composed abstract landscapes.
- Fluid, continuous geometry.
- Rich, spatially coherent color fields.
- Strong large-scale shapes with intricate smaller-scale detail.
- Material qualities that vary intentionally across a surface.
- Spatial depth that remains legible without relying on a black background.
- Animation with rhythm, continuity, anticipation, and release.
- A recognizable artistic identity for each scene.

The result does not need to imitate a specific commercial VFX package or another artist's work. It should use the strengths of AV Gen's own rendering and resonator systems to achieve a comparable level of intentionality.

## 2.1 Visual hierarchy

Every frame should have a clear visual hierarchy:

1. **Primary visual event:** the main resonant structure, wavefront, or flowing form.
2. **Secondary structure:** related forms that create contrast, scale, depth, and visual relationships.
3. **Environmental field:** background structures, atmospheric color, distant patterns, or surrounding space that make the world feel continuous.
4. **Fine detail:** small ridges, interference lines, local material variation, and delicate optical features.

The primary structure should remain readable. Secondary structures should enrich it rather than compete with it. Fine detail should reward closer inspection without turning the entire image into visual noise.

Avoid both extremes:

- One oversized object filling the frame without meaningful spatial context.
- A collection of tiny, disconnected objects scattered across an empty background.

Use contrast in scale, density, brightness, motion, and color to guide attention.

## 2.2 Design at multiple spatial scales

Each scene should have at least three intentionally designed spatial scales:

- **Global scale:** the overall silhouette, field, landscape, or spatial arrangement.
- **Medium scale:** distinct wave structures, lobes, folds, branches, ridges, or interference features.
- **Fine scale:** smaller surface detail, delicate filaments, subtle ripples, or localized color transitions.

These scales should be related, but not identical copies of one another.

Fine-scale detail should not simply be higher-frequency noise layered on top of a generic surface. It should emerge from the underlying visual system.

At any given moment, a viewer should be able to perceive both the large-scale composition and the intricate detail.

---

# 3. Material and Lighting Overhaul — Shared Foundation

All three scenes need a more sophisticated material and lighting language.

This should be implemented before relying on elaborate scene-specific effects, because geometry and color will not look convincing if the material response washes out their identity.

## 3.1 Eliminate the generic glossy-plastic appearance

The current surfaces too often resemble polished plastic: smooth highlights, overly uniform gloss, dark creases, and large regions whose apparent color is determined by reflection rather than by the actual surface color.

Investigate the balance between:

- Base color.
- Diffuse or equivalent broad surface response.
- Specular intensity.
- Roughness.
- Fresnel response.
- Emissive contribution.
- Environmental illumination.
- Normal variation.
- Reflection and post-processing behavior.

Use only the material controls actually supported by the engine.

### Required material principles

**The underlying color must remain beautiful without a strong specular highlight.**

Temporarily test representative materials with specular contribution reduced or disabled. If the surfaces become visually dull or lose all their color identity, the base color, illumination, or shading model needs work.

Use specular reflection as a secondary visual feature, not as the principal source of perceived color.

Develop a deliberate balance between:

- Saturated body color.
- Broad, soft illumination.
- Restrained directional highlights.
- Subtle roughness variation.
- Controlled emission where appropriate.
- Fine surface structure that responds to light.

Do not make every material matte. Do not make every material glossy. Material variety should be part of the composition.

For example, a scene might combine a softly luminous base field, a moderately reflective primary surface, delicate translucent-looking or emissive filaments, and darker but still chromatically rich regions.

Do not introduce expensive true transparency, transmission, or physically based subsurface scattering if the existing renderer cannot support them efficiently. Approximate the desired appearance using techniques appropriate to the current pipeline.

### Avoid

- Large white highlights that erase saturated colors.
- Pure black grooves and creases that make the geometry resemble rubber or plastic.
- Uniform roughness across every surface.
- A single global metallic setting used to make everything look sophisticated.
- Excessive Fresnel rims.
- Neon outlines around every object.
- Emission so strong that the geometry becomes a flat, self-illuminated silhouette.

## 3.2 Make illumination chromatic

Lighting should reinforce the palette rather than wash it out.

Where supported, introduce broad, colored illumination from multiple directions or through environment lighting. Use contrasting light colors to reveal different sides of the geometry.

The exact implementation depends on the renderer, but the intended result is:

- Surfaces retain their underlying saturated hues.
- Broad illumination reveals form without creating harsh white plastic highlights.
- Different orientations reveal different color relationships.
- Recesses retain some chromatic identity rather than collapsing to black.
- Emissive features influence their immediate surroundings where practical.

Do not add dozens of dynamic lights to achieve this. Prefer a small, carefully designed lighting arrangement, procedural environment illumination, or other efficient techniques already compatible with AV Gen.

If actual light transport is unavailable or too expensive, use shader-based approximations that preserve spatial coherence.

## 3.3 Introduce purposeful surface variation

Uniform surfaces look synthetic in an uninteresting way. Add carefully controlled variation to appropriate material parameters.

Potential dimensions include:

- Broad roughness variation.
- Fine roughness variation.
- Surface-color variation.
- Orientation-dependent coloration.
- Subtle modulation from the resonator field.
- Fine ridge or groove response.
- Variation in emission intensity.

These variations must be coherent and art-directed.

They should follow meaningful spatial features, resonant structures, or flowing coordinates. Avoid independent random values per vertex or fragment.

Variation should make a surface feel intricately designed, not dirty or noisy.

## 3.4 Protect color saturation through the entire rendering pipeline

Inspect the full path from shader output through HDR accumulation, bloom, tone mapping, and final display.

Verify whether the current color palette is being compromised by:

- Excessive white illumination.
- Overexposure.
- Bloom that spreads bright colors into white.
- Tone mapping that compresses saturated highlights.
- Overly dark exposure.
- Palette interpolation that passes through undesirable neutral colors.

Retain the existing rendering architecture unless there is a compelling reason to change it.

Use the existing tone mapper and post-processing controls where possible. Do not implement an entirely new color-management system just for these scenes.

The target is **rich, luminous color with visible internal structure**, not maximum brightness.

## 3.5 Bloom and optical effects

Bloom should be restrained and selective.

It should emphasize a limited number of bright ridges, resonant nodes, filaments, or traveling wavefronts.

Preserve the original surface geometry and color beneath the bloom. Fine features should not disappear into a fog of light.

If the engine supports depth-aware fog or volumetric approximations, use them to reinforce depth where appropriate. Avoid filling the entire image with uniform haze.

Chromatic aberration, distortion, and other post-processing should be optional and very restrained. They must not become the primary source of visual complexity.

Do not add every available post-processing effect simply because it is available.

---

# 4. Eliminate Empty Black Space — Build Continuous Visual Environments

A major problem across the three scenes is that the main geometry often appears to be floating in a black void.

A dark background is not automatically bad. The problem is **unintentional emptiness**.

We want environments whose surrounding space participates in the artwork.

The background may be darker than the foreground, but it should have designed color, depth, structure, and temporal behavior.

## 4.1 Background requirements

Each scene must have a coherent environmental treatment.

Depending on the scene, this could include:

- A continuous procedural color field.
- Distant repetitions or continuations of the main resonant structures.
- Broad atmospheric gradients.
- Layered wavefronts.
- Softly illuminated distant geometry.
- Low-contrast interference patterns.
- Subtle moving bands of light.
- Depth-dependent color shifts.
- Fine structures that gradually fade into the distance.

Do not solve the problem by putting a flat colored plane behind the existing geometry.

The environment should feel connected to the main scene's visual logic.

## 4.2 Use contrast instead of an empty void

A dark background can still be visually rich.

Use controlled variation in hue and brightness so that dark regions have identity. Deep indigo, violet, burgundy, and other palette-appropriate colors can create depth without requiring high brightness.

Avoid large, uniform regions of absolute black unless they are deliberately framed negative space.

Where a scene needs visual breathing room, preserve it through lower detail density, reduced brightness, and slower motion—not necessarily by removing all environmental information.

## 4.3 Background animation

Background animation must be subtle enough to support the foreground but meaningful enough to prevent the world from appearing static.

Use slow traveling gradients, distant wave motion, evolving interference patterns, or related effects.

The background should not merely flash in time with the beat. It should participate in the same underlying musical and resonant system at a different spatial scale and temporal rate.

Use depth, contrast, and motion to keep the primary visual event legible.

---

# 5. Cymatic World — A Genuine Resonant Pattern World

## Artistic objective

Cymatic World must become a world of diverse, intricate, evolving resonant patterns—not a permanently radial cellular surface that happens to move and change color.

The most important issue is the underlying geometry.

The existing scene has improved in its audio-reactive movement, but it remains too dominated by a repeated circular or crater-like topology. Changing the amplitude of that same pattern is insufficient.

**We need genuinely different families of spatial patterns generated by the resonator system.**

The visual inspiration is the richness of cymatic phenomena: nodal lines, lobes, standing-wave structures, interference patterns, branching boundaries, and complex relationships between different modes.

This is an artistic interpretation, not a requirement to simulate a physical plate with scientific accuracy.

## 5.1 Redesign the geometry around modal diversity

Inspect the existing geometry-generation algorithm and determine why it produces the same radial or cellular structure so consistently.

If necessary, redesign the spatial basis used to construct the resonant field.

The field should support multiple families of patterns, including:

### A. Radial and angular modes

Patterns based on combinations of radial oscillations and angular structure.

These can produce:

- Concentric regions with different numbers of rings.
- Petal-like lobes.
- Alternating radial ridges and depressions.
- Asymmetric combinations of radial and angular features.
- Complex interference between patterns with different angular counts.

Do not make every pattern a flower or a target. These are only one family.

### B. Chladni-inspired nodal patterns

Patterns containing coherent nodal lines, branching boundaries, lobes, and regions separated by relatively stable low-amplitude structures.

Explore:

- Curved nodal lines.
- Intersecting nodal structures.
- Multi-lobed configurations.
- Asymmetric standing-wave arrangements.
- Dense but legible networks of boundaries.
- Broad regions connected by narrow resonant corridors.

The geometry should communicate organized resonance rather than random terrain displacement.

### C. Interference fields

Combine several distinct spatial modes.

For example, a broad asymmetric mode can interact with a finer angular mode and a third slowly varying component.

The combination should produce meaningful changes in the overall topology and surface structure.

Use controlled relative amplitudes, frequencies, phases, and damping. Maintain phase continuity as the mixture evolves.

Avoid simply adding several independent noise textures.

### D. Traveling and standing-wave combinations

The field should be capable of blending between relatively stable standing patterns and traveling interference features.

These should create changes in the apparent direction, shape, and connectivity of the pattern without making the entire surface twitch.

### E. Multiscale resonances

Combine a few distinct, deliberately selected spatial scales.

A large-scale mode should define the overall shape. Medium-scale modes should create identifiable secondary patterns. Fine modes should reveal intricate details.

Do not allow fine-scale modes to dominate the global silhouette.

## 5.2 Build a reusable library of mode families

Create an appropriate internal representation of spatial mode families.

The exact data structure should fit the existing codebase, but the system should support:

- Stable mode identity.
- Spatial frequency or complexity.
- Amplitude.
- Phase.
- Damping or persistence.
- Relationships to audio-derived signals.
- Smooth transitions between selected mode combinations.

The library should produce meaningfully different patterns, not just different parameter values for the same pattern.

Explore several substantially different families during development. Select the most compelling ones for the final scene.

Avoid exposing a huge collection of low-level mode parameters to the end user. The implementation can be sophisticated while the final performance controls remain simple.

## 5.3 Make musical input change the pattern's behavior

Audio should influence the resonant field in several complementary ways.

Use the existing audio analysis and signal infrastructure. Do not introduce a parallel audio-analysis system if the current one provides the required information.

Potential mappings:

- Low-frequency energy excites broad, large-scale modes.
- Mid-frequency energy excites medium-scale lobes and interference patterns.
- High-frequency energy influences fine structure, ridges, and local detail.
- Transients excite a controlled response in selected modes.
- Sustained energy increases or maintains excitation.
- Silence allows the system to settle, decay, or continue a quieter residual motion.

These are starting points, not rigid requirements. Tune the mappings by listening and observing actual rendered output.

The field should have a sense of memory: a musical event should excite a pattern that persists and evolves, rather than appearing for one frame and disappearing.

Use smoothing, damping, and attack/release behavior appropriate to each scale.

## 5.4 Change topology and composition, not just displacement

The current scene can appear to preserve its basic shape even when its surface moves.

Correct this.

Allow the relationships between dominant lobes, nodal boundaries, and interference structures to change over time.

For example, a broad radial configuration might gradually transition into an asymmetric multi-lobed field, then into a more intricate network of intersecting patterns.

Transitions should be continuous. Do not abruptly replace the entire mesh or randomly shuffle the scene's geometry on each beat.

The system should be able to generate several recognizable visual identities within one performance.

The geometry itself must carry the variation. Color changes alone do not count as a new pattern.

## 5.5 Geometry quality and detail

Avoid both a coarse, smooth terrain and a uniformly noisy surface.

The desired geometry contains:

- Large-scale coherent displacement.
- Medium-scale ridges, valleys, lobes, and boundaries.
- Fine-scale features that follow the resonant structure.
- Clear transitions between peaks and nodal regions.
- Smooth normals and stable shading.
- Enough detail to reward close inspection without losing the overall pattern.

Use the existing GPU or CPU geometry pipeline where appropriate.

Do not increase mesh density indiscriminately. Prefer efficient procedural evaluation or other existing rendering techniques where they can provide the required detail.

Pay particular attention to aliasing, temporal shimmer, and unstable normals.

## 5.6 Camera and framing

Pull back from the current excessively close view.

The viewer must be able to understand the complete resonant configuration while still seeing fine detail.

Establish a camera distance that reveals:

- The overall pattern.
- Its medium-scale structures.
- Enough fine surface detail to make the world feel intricate.

Use a composed angle rather than an arbitrary top-down or excessively grazing view.

A slightly elevated oblique perspective may be useful if it reveals depth without obscuring the nodal pattern. Test actual alternatives.

Avoid a horizonless close-up that makes it impossible to understand the geometry's overall organization.

The surrounding environment should provide spatial context and may include a continuation of the resonant field or a complementary background pattern.

Do not simply shrink the existing object and leave it floating in black space.

## 5.7 Cymatic World color identity

Retain and expand the scene's custom palette system.

Develop a collection of palettes that suit resonant geometry. Explore contrasting combinations of:

- Crimson and deep indigo.
- Magenta, violet, and midnight blue.
- Saturated red, wine, and electric violet.
- Gold, ember, and deep plum.
- Other carefully designed combinations that complement the scene.

Do not assume that cyan, teal, or green are required. Use them only if they genuinely improve a specific palette.

Color should reinforce the modal structure:

- Nodal boundaries may be darker, less emissive, or differentiated by hue.
- Major ridges may receive saturated color transitions.
- Broad lobes may carry flowing gradients.
- Fine resonant details may carry subtle local variation.
- High-energy areas may become brighter without all collapsing to white.

Avoid coloring every ridge with the same neon outline.

Develop transitions between palettes and between dominant color regions. Palette changes should be smooth, musical, and visually coherent.

## 5.8 Acceptance test for Cymatic World

Before considering the scene complete, capture several frames with substantially different resonant configurations.

Ask:

- Does each frame exhibit a recognizably different spatial pattern?
- Can I identify multiple distinct mode families?
- Is the geometry still interesting with a neutral material?
- Can I understand the overall composition without zooming in?
- Does the pattern have coherent nodal structures rather than generic blobs?
- Does audio excite and sustain the pattern in a meaningful way?
- Is the movement expressive rather than twitchy?
- Does the background feel connected to the scene?
- Is the color rich without being dependent on white highlights?

If the scene still looks like the same radial crater with different deformations, it has not passed.

---

# 6. Resonant Silk — An Immersive Flowing Material Environment

## Artistic objective

Resonant Silk should feel like an expansive, flowing, interconnected world made from elegant, light-responsive material.

The current composition reads too much like a pair of glossy sheets or ribbons that converge at a pinch, surrounded by black space.

The solution is not to add more random ribbons.

The scene needs a more sophisticated spatial composition, richer material behavior, and a continuous environment that makes the flowing forms feel integrated into a larger whole.

## 6.1 Develop a family of related flowing structures

Retain the strongest aspects of the existing flowing geometry, but expand the visual vocabulary.

Potential forms include:

- Broad flowing sheets.
- Narrower folds and filaments.
- Interconnected surface layers.
- Branching or diverging wave-like structures.
- Overlapping folds with distinct depths.
- Broad undulations with finer rippling detail.
- Structures that pass behind and in front of one another.

Use these as a coherent family rather than a collection of unrelated objects.

The overall form should feel intentionally designed. It may be abstract, but it should not look like random spaghetti, disconnected ribbons, or a pile of generic cloth simulations.

## 6.2 Improve the spatial composition

Create a foreground, middle ground, and background.

The foreground can contain a small number of prominent folds or flowing surfaces.

The middle ground should establish the primary composition, with overlapping structures that guide the eye through the scene.

The background should continue the visual language through distant, softer forms or flowing fields.

Not every layer should be equally bright, detailed, or fast.

Use variation in scale, orientation, density, curvature, and motion to create depth.

Allow forms to enter and leave the frame in an intentional manner. The scene should feel larger than the visible camera view.

Do not simply clone the current sheet several times with different offsets.

## 6.3 Motion should be fluid and coherent

Develop a continuous flow system for the surfaces.

Potential inputs include:

- Broad traveling waves.
- Slowly evolving curvature fields.
- Secondary ripples.
- Resonant deformations.
- Controlled phase offsets between related layers.
- Audio-responsive excitation.

Use the resonator concept where it provides value, but do not force every motion component to behave like a cymatic standing wave.

The scene's identity is flowing, layered motion.

A strong bass transient might send a broad deformation through several layers. Midrange energy might excite finer folds. High-frequency energy might introduce delicate ripples or localized illumination.

These responses should remain subordinate to the main flow.

Motion must have continuity. Avoid arbitrary per-frame changes, unfiltered audio jitter, and uncorrelated movement between neighboring vertices.

## 6.4 Material identity: luminous silk, not polished plastic

Create a distinctive material response that combines rich color, soft light, controlled reflection, and subtle surface variation.

The surface should feel silky and luminous without relying on exaggerated specular highlights.

Explore:

- Broad color gradients that follow the flowing surface.
- Directional color shifts across folds.
- Subtle rim illumination.
- Restrained emission in selected regions.
- Fine variations in roughness or surface response.
- Broad colored light that reveals the folds.
- Subtle secondary detail along selected creases.

Do not apply all these effects uniformly.

Some regions should be visually calm and richly colored. Others can contain brighter, finer details.

Avoid excessive metallicness, uniform mirror-like reflection, white streaks that erase the palette, and neon outlines along every edge.

## 6.5 Make color flow through the material

Color should evolve along the flowing structures and through time.

Create coherent color fields that can move along the surface's natural coordinates or through a suitable procedural mapping.

Use a combination of:

- A broad base gradient.
- A secondary flowing color field.
- Subtle detail that follows curvature or resonant features.
- Slowly changing palette relationships.
- Selective luminous accents.

The exact technique should fit the existing geometry and shader implementation.

Avoid a simple animated rainbow gradient, arbitrary UV color stripes, or independent noise applied to each vertex.

The colors should feel as though they belong to the material.

Develop multiple distinctive palettes suitable for this scene. Palette changes should alter the mood without destroying the material's identity.

## 6.6 Integrate the environment

The environment should feel like an extension of the flowing material world.

Options include distant continuation of the flow, large softly colored wave fields, faint layered structures, or a procedural background whose gradients and movement respond to the same underlying temporal system.

Keep the background visually subordinate.

Do not place the existing ribbons in front of a flat gradient and declare the scene finished.

The viewer should perceive an immersive space with a coherent visual language, even if the space is entirely abstract.

## 6.7 Camera movement

Use subtle camera movement if it improves depth and scale.

Potential approaches include slow drift, restrained orbital motion, or gradual movement through the layered structures.

Avoid constant spinning, aggressive camera shake, random zooming, and camera movement that obscures the artwork.

The geometry and its internal motion must remain the principal sources of visual interest.

## 6.8 Acceptance test for Resonant Silk

The scene should:

- Feel spatially expansive.
- Contain more than one or two visually dominant sheets.
- Have a clear foreground, middle ground, and background.
- Show convincing overlap and depth.
- Exhibit fluid, continuous motion.
- Have rich color even when specular reflection is reduced.
- Use highlights selectively.
- Maintain an active but restrained environment.
- Remain elegant rather than cluttered.
- Look compelling when paused on several different frames.

If it still reads as two glossy ribbons in a black void, the scene has not passed.

---

# 7. Wave Form Dimension — A World Built from Traveling Waves

## Artistic objective

Wave Form Dimension should be the most overtly wave-driven of the three scenes.

It should feel like a continuous spatial environment in which large wave structures travel, overlap, interfere, fold, and create changing patterns.

The existing geometry has some useful qualities, but the overall image still feels too isolated and static in its surroundings.

The solution is to make the environment participate in the wave system and establish a stronger relationship between large-scale waves and finer interference detail.

## 7.1 Establish a coherent wave vocabulary

Develop several related types of wave structure:

- Broad traveling wavefronts.
- Intersecting waves.
- Standing-wave regions.
- Interference bands.
- Folded or undulating surfaces.
- Smaller ripples riding on larger waves.
- Distant wave structures that establish scale.
- Selective high-energy ridges or crests.

These should be manifestations of a coherent system, not unrelated shapes.

The scene should clearly communicate propagation, interaction, and continuous motion.

Avoid relying on a single sine wave or a uniformly displaced plane.

## 7.2 Build the scene at multiple scales

At the largest scale, establish a clear spatial composition of major wave structures.

At the medium scale, create interference patterns, folds, and crossing wavefronts.

At the fine scale, add restrained ripples and details that respond to the larger structures.

These scales should interact without becoming indistinguishable noise.

Broad wavefronts must remain legible. Fine detail should follow their movement and structure.

## 7.3 Make wave interaction visually meaningful

Wave intersections should produce recognizable changes in amplitude, curvature, brightness, or color.

Explore how traveling waves and standing-wave regions interact.

Use phase relationships and controlled superposition to produce patterns that change over time.

The visual result should be more interesting than adding several sine functions together and applying an animated rainbow material.

Give wave intersections a clear visual identity. For example, selected interference regions might develop sharper ridges, brighter color, or more intricate surface detail.

Do not highlight every intersection equally.

## 7.4 The environment must also be animated

This is a critical requirement.

The background cannot remain a static black plane or a flat, unchanging color.

Develop an environmental field that participates in the scene's wave logic.

Depending on the existing rendering architecture, this might be:

- A distant continuation of the wave field.
- A layered field of softer traveling waves.
- Broad animated color gradients.
- Distant interference patterns.
- Low-contrast wave structures at different scales.
- A subtle atmospheric environment with moving color and light.

The foreground and background should share visual principles but not identical detail density.

Use the background to communicate scale and continuity.

The viewer should feel that the waves extend beyond the immediate camera view.

Avoid making the environment compete with the primary wavefronts.

## 7.5 Develop a distinctive material response

Wave Form Dimension should not simply reuse Resonant Silk's material unchanged.

Its material identity should emphasize wave structure, traveling energy, interference, and surface curvature.

Possible features include:

- Rich color gradients that follow wave coordinates.
- Color changes related to local wave phase or amplitude.
- Selective luminous ridges.
- Restrained reflective highlights.
- Fine details aligned with the wave direction.
- Controlled color contrast between crests and troughs.

Maintain visible body color and avoid the same generic plastic appearance identified in the other scenes.

Some parts of the wave field can be smooth and luminous. Others can be more subdued, allowing the dominant structures to stand out.

## 7.6 Audio response

Use the existing signal architecture to drive the wave system.

Potential mappings include:

- Bass energy influencing broad wave amplitude and propagation.
- Midrange energy influencing interference complexity and medium-scale structure.
- High-frequency energy influencing fine ripples and selected luminous details.
- Transients exciting traveling disturbances.
- Sustained energy controlling the persistence or intensity of wave activity.

Do not translate every audio feature directly into geometric displacement.

Use smoothed control signals and distinct response times so that the waves feel musical rather than jittery.

The environment should respond at a slower or broader scale than the finest foreground detail.

## 7.7 Camera and composition

Use a camera angle that reveals wave propagation and spatial depth.

Avoid framing that turns the entire scene into an undifferentiated field of folded geometry.

The image should have a clear primary direction or compositional structure, while still allowing the viewer to discover smaller wave interactions.

Use depth layering, silhouette, and contrast to separate the foreground from the distance.

Camera motion should be restrained and purposeful.

## 7.8 Acceptance test for Wave Form Dimension

The scene should:

- Communicate traveling waves immediately.
- Contain meaningful interactions between wave structures.
- Show large-, medium-, and fine-scale detail.
- Have a living environment rather than a static black background.
- Retain rich surface color under reduced specular reflection.
- Exhibit continuous and musically responsive motion.
- Have a clear composition at a glance.
- Look distinct from both Cymatic World and Resonant Silk.

If the scene still reads as glossy folded shapes floating in empty space, the overhaul is incomplete.

---

# 8. Shared Color-Field Architecture

We need sophisticated gradients and color variation, but we should not make all three scenes look alike.

Investigate whether a small reusable set of procedural color-field utilities can serve all three scenes while allowing each to retain its own identity.

Potential reusable capabilities include:

- Smooth multicolor palette interpolation.
- Broad spatial gradients.
- Secondary interference-driven color fields.
- Fine coherent color variation.
- Time-evolving palette transitions.
- Color modulation based on geometry, curvature, phase, or amplitude.
- Separate controls for saturation, luminance, and emission.

Use the simplest architecture that fits the existing engine.

Do not create a massive generic material framework just for these scenes.

## 8.1 Build continuous gradients, not discrete color bands

Use smooth interpolation across multiple palette stops.

Choose an appropriate interpolation method for the current color pipeline. Avoid abrupt transitions, muddy intermediate colors, or unnecessary desaturation.

Consider perceptually smoother interpolation if it is practical within the existing shader architecture.

Inspect the actual rendered output rather than assuming a mathematically smooth interpolation will automatically look good.

## 8.2 Layer the color fields

A useful conceptual model is:

1. **Primary field:** defines the broad dominant color regions.
2. **Secondary field:** introduces another coherent spatial pattern.
3. **Geometry-aware field:** reinforces curvature, wave phase, modal structure, or local features.
4. **Fine variation:** provides subtle detail.
5. **Temporal evolution:** changes the relationships between these fields over time.

Not every scene needs all five layers at full strength.

The primary field should remain recognizable. Secondary fields should create visual richness, not visual clutter.

Do not simply multiply several high-frequency procedural textures together.

## 8.3 Palette transitions

Each scene should have multiple authored palettes with clearly different identities.

Palettes should differ in more than a slight hue rotation.

Vary the relationships between dominant, secondary, shadow, and highlight colors.

Transitions should be smooth and deliberate. Use stable interpolation between palette states rather than switching abruptly on beats.

The system should support audio-reactive palette evolution without turning into constant random color cycling.

## 8.4 Color should reinforce the underlying system

For Cymatic World, color should reveal modal patterns and nodal structures.

For Resonant Silk, color should flow along the material and reinforce its folds.

For Wave Form Dimension, color should communicate traveling waves and interference.

Shared utilities are acceptable. Shared artistic results are not required.

---

# 9. Temporal Evolution and Audio-Reactive Behavior

A scene can contain good geometry and good colors yet remain visually uninteresting if the composition does not evolve.

All three scenes need coherent temporal behavior at multiple scales.

## 9.1 Separate response times

Avoid driving every visual parameter directly from the same instantaneous audio value.

Use the existing envelope followers, modulators, or other suitable signal tools to create distinct response characteristics.

For example:

- Large-scale structures may respond more slowly and retain changes longer.
- Medium-scale structures may react more directly to musical phrases.
- Fine details may respond to transients but should remain controlled.
- Color transitions may evolve more slowly than local geometry.
- Environmental fields may follow long-term energy or harmonic movement.

Tune these relationships by inspecting actual performance material.

## 9.2 Preserve continuity

All temporal changes should remain stable across frames.

Avoid:

- Randomly regenerated geometry every frame.
- Abrupt phase resets.
- Unfiltered high-frequency audio driving large displacements.
- Instantaneous palette changes.
- Independent movement that destroys coherent relationships between adjacent structures.
- Rapid flicker caused by threshold crossings.

Use persistent state, stable phase progression, smoothing, damping, and interpolation as appropriate.

Do not over-smooth the response until the scenes feel disconnected from the music.

The goal is expressive responsiveness with continuity.

## 9.3 Create musical arcs

The scenes should have enough internal evolution to remain interesting through a sustained performance.

Develop a balance between:

- Stable visual identities.
- Phrase-level changes.
- Event-driven responses.
- Gradual transitions.
- Quiet periods.
- High-energy passages.

A scene should not need constant camera motion or random pattern switching to remain alive.

Use the resonator system's persistence and relationships to create the sense that musical energy has consequences over time.

## 9.4 MIDI and manual control

Preserve compatibility with existing audio, MIDI, and parameter systems.

Do not add new controls merely because a parameter exists internally.

Prefer a small number of meaningful controls that alter the scene's character.

Where the current architecture supports MIDI mapping, ensure new parameters remain controllable through existing mechanisms.

Avoid breaking the existing live-performance workflow.

---

# 10. Make the Three Scenes Visually Distinct

The shared rendering and resonator infrastructure must not force these scenes into a common visual style.

The three scenes need clearly different visual identities.

| Dimension | Cymatic World | Resonant Silk | Wave Form Dimension |
|---|---|---|---|
| Primary identity | Organized resonance and nodal geometry | Flowing, layered material | Traveling waves and interference |
| Main visual event | Evolving modal patterns | Continuous folds and flowing surfaces | Propagating wavefronts |
| Motion character | Resonant, structured, damped | Fluid, graceful, interconnected | Propagating, rhythmic, intersecting |
| Spatial composition | Readable overall field with intricate detail | Immersive layered environment | Expansive wave environment |
| Color behavior | Reveals modal structures | Flows through the material | Reinforces phase and propagation |
| Fine detail | Nodal ridges and local resonant features | Fine folds and subtle surface variation | Ripples and interference detail |
| Main failure to avoid | Repeated radial craters and twitchy blobs | Two glossy ribbons in a void | Glossy folds on a static background |

After implementation, compare the three scenes side by side.

If they look like the same geometry with different palettes, revisit their underlying systems.

---

# 11. Performance and Engineering Constraints

AV Gen is a live audiovisual instrument. Visual quality must be achieved within its real rendering constraints.

Use the existing WebGPU/Dawn rendering architecture and supported platforms.

Do not rewrite the renderer or introduce a new graphics backend for this task.

## 11.1 Profile the actual implementation

Capture representative GPU and CPU timings before and after the work where tooling permits.

Inspect the cost of:

- Additional geometry.
- Vertex processing.
- Fragment shading.
- Procedural color evaluation.
- Background rendering.
- Extra rendering passes.
- Bloom and other post-processing.
- Dynamic material updates.
- CPU-side geometry generation or deformation.

Do not assume that a shader is inexpensive simply because it is compact.

Avoid unnecessary full-screen passes, excessive per-pixel trigonometric evaluation, redundant calculations, and indiscriminate geometry subdivision.

Use existing performance instrumentation and optimize demonstrated bottlenecks.

## 11.2 Preserve the live-rendering target

Use the project's established performance target and test configurations.

A beautiful still image is not sufficient if the scene becomes unusable for live performance.

Prefer coherent procedural shading and efficient geometry representations over brute-force mesh density.

Where quality settings exist, preserve graceful degradation at lower settings.

Do not degrade all three scenes to the lowest common denominator. They can share utilities while using scene-appropriate rendering complexity.

## 11.3 Avoid architecture sprawl

Before creating new abstractions, check whether existing scene, shader, material, parameter, audio, and rendering systems already provide what is needed.

Share utilities only when the shared abstraction is genuinely useful.

Keep scene-specific behavior close to the scene where practical.

Do not create a new generic framework with dozens of options merely to avoid writing three focused implementations.

Do not add compatibility shims or retain obsolete branches without a demonstrated need.

Clean up superseded implementations when doing so is safe and consistent with repository conventions.

---

# 12. Required Iteration and Visual Evaluation

This section is critical.

Do not implement all the proposed features at once and assume the combination will look good.

Use staged iteration so that weak geometry, materials, and composition can be identified independently.

## Stage A — Geometry without decorative shading

For each scene, temporarily use a simple neutral material and restrained lighting.

Evaluate whether the geometry and composition are compelling on their own.

For Cymatic World, render multiple distinct modal configurations.

For Resonant Silk, inspect the layered flowing composition.

For Wave Form Dimension, inspect the structure and interaction of the traveling waves.

If the geometry is not interesting without elaborate shading, improve it before moving on.

## Stage B — Composition and environment

Establish camera framing, scale, depth, and background treatment.

Temporarily reduce material complexity if necessary.

Verify that the scene feels complete rather than isolated against black.

## Stage C — Material and color

Develop the color fields, roughness, lighting, emission, and selective optical effects.

Compare renders with specular reflection reduced to confirm that the base colors are doing enough work.

Test multiple palettes.

## Stage D — Temporal behavior

Render sequences rather than only single frames.

Inspect how the composition evolves through:

- Sustained low energy.
- A transient.
- A sustained energetic passage.
- A quieter passage.
- A transition between different pattern states or wave configurations.

Use deterministic test signals where available so that comparisons are reproducible.

Verify that animation remains coherent and avoids twitchiness.

## Stage E — Final polish

Tune the balance between detail and clarity.

Remove features that add cost without contributing visible value.

Correct distracting artifacts, unstable shading, excessive bloom, aliasing, poor silhouettes, and unintentional clipping.

Only add further effects if they solve an identified problem.

---

# 13. Render Review Checklist

For each scene, capture a consistent set of review images:

1. A representative hero frame.
2. A substantially different frame showing another configuration.
3. A frame during a high-energy passage.
4. A frame during a quieter passage.
5. A frame using an alternative palette.
6. A neutral-material frame that reveals the underlying geometry.

Also capture a short sequence or video where practical.

Use the same resolution, rendering settings, and comparable exposure for before/after comparisons.

Evaluate the actual images against these questions.

### Composition

- Is there a clear focal point?
- Can I understand the overall composition immediately?
- Is there meaningful foreground, middle ground, and background where appropriate?
- Does the environment feel continuous?
- Is the camera distance appropriate?
- Is there intentional visual breathing room?

### Geometry

- Are there distinctive, recognizable structures?
- Does the geometry have more than one spatial scale?
- Is the shape language specific to this scene?
- Is the variation structural rather than merely random?
- Does the geometry remain compelling without decorative shading?

### Materials

- Are the surfaces still colorful when specular reflection is reduced?
- Are highlights controlled?
- Do materials have differentiated visual character?
- Is surface detail coherent?
- Does the geometry read clearly under the lighting?

### Color

- Are the gradients smooth and rich?
- Do the colors remain saturated without clipping?
- Are the palettes meaningfully distinct?
- Does color reinforce the geometry?
- Are large regions of color visually intentional?

### Animation

- Does the scene evolve over time?
- Are changes coherent?
- Do different spatial scales respond differently?
- Is there a clear relationship to music?
- Does the scene avoid twitching, flickering, and arbitrary motion?

### Performance

- Does the scene render within the intended live budget?
- Are new GPU or CPU costs understood?
- Are there obvious opportunities to remove unnecessary work?
- Does the scene degrade gracefully where the existing engine supports quality settings?

---

# 14. Explicit Anti-Patterns

Do not use any of the following as a substitute for the requested work.

- More random noise applied to the current geometry.
- More random per-frame deformation.
- A larger number of copies of the same ribbon.
- Additional radial rings that preserve the existing crater-like pattern.
- Constant camera spinning.
- A flat colored background used to disguise the black void.
- A static background plane with no relationship to the scene.
- A rainbow shader applied uniformly to every surface.
- Simple hue rotation presented as a complete color system.
- More bloom used to compensate for weak color.
- Stronger specular highlights used to make a surface look expensive.
- Neon outlines on every ridge.
- More particles added without a clear compositional role.
- Excessive detail that destroys the readability of the main structure.
- A physically expensive simulation when a stable procedural approximation would produce the desired artistic result.
- A large generic abstraction that makes the scenes harder to develop.
- A completed implementation declared successful without inspecting its rendered output.

If a technique does not improve the actual image, remove it or revise it.

---

# 15. Implementation Deliverables

Complete the work in the repository. Do not stop after writing a plan or presenting a design proposal.

Deliver:

- Three improved scenes, each with a clearly differentiated visual identity.
- Improved material and lighting behavior, reducing the generic plastic appearance and preserving rich body color.
- Designed, animated environments that eliminate the feeling of isolated objects in an empty black void.
- A substantially more diverse Cymatic World, with genuinely different modal pattern families and improved framing.
- An immersive Resonant Silk, with layered flowing geometry and a coherent surrounding environment.
- A more expansive Wave Form Dimension, with meaningful traveling-wave interactions and an animated environment.
- Reusable utilities where appropriate for palettes, color fields, or related capabilities.
- Smooth audio-reactive temporal behavior, using the existing signal infrastructure.
- Visual comparison artifacts, preferably before/after images and representative animation captures.
- Build and test results, including any relevant scene-specific or rendering tests.
- Performance measurements, where reliable instrumentation is available.
- Documentation describing the new systems, the key design decisions, and any relevant controls or limitations.

Keep generated screenshots, comparison sheets, or other review artifacts in an appropriate existing project location. Do not commit huge temporary render sequences or unrelated generated files.

Do not leave experimental branches of the implementation disconnected from the scenes. Ensure the intended systems are actually used by the final scene implementations.

## Final report

At completion, report:

- What changed in each scene.
- What underlying geometry or visual systems were redesigned.
- What was shared and what remains scene-specific.
- Which palette and material improvements were implemented.
- How the backgrounds and spatial composition changed.
- How audio and temporal behavior work.
- Which rendered comparisons were inspected.
- What the visual iteration revealed and what was revised as a result.
- Build and test results.
- Performance results and any regressions.
- Remaining shortcomings or work that could not be verified.

Be candid. Do not describe an improvement as successful solely because the implementation compiles.

## Final Artistic Principle

The goal is not to make the current images brighter, shinier, busier, or more complicated.

The goal is to make the underlying visual systems more expressive.

Cymatic World should generate beautiful and diverse resonant patterns.

Resonant Silk should feel like an immersive world of flowing, light-responsive material.

Wave Form Dimension should feel like an expansive environment built from traveling waves and interference.

All three should have intentional composition, rich spatial color, controlled lighting, meaningful temporal evolution, and a sense that the environment belongs to the same visual world as its primary geometry.

Prioritize, in order:

1. Compelling underlying geometry.
2. Strong composition and spatial depth.
3. Rich, coherent color.
4. Sophisticated material and lighting response.
5. Continuous, meaningful animation.
6. Restrained final effects and performance polish.

If the scene is beautiful with neutral shading, it has a strong foundation. If its colors remain compelling with specular reflection reduced, it has a strong material foundation. If its environment feels alive without competing with the foreground, it has a strong compositional foundation.

Build those foundations first, then combine them into three distinctive, performance-ready audiovisual worlds.
