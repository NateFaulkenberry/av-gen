> Owner's brief, saved verbatim on 2026-10-20, as more art direction for the Rainbow World pass-2 agent. It adds
> to 13-brief-stem-reactivity.md and to the owner's two notes after gate 2:
> 1. The per-kick world ring is too much. A world scan should be rare, dramatic and tied to synth swells, turning
>    everything it touches rainbow-coloured.
> 2. The camera should feel like a roller coaster: flying, flipping, diving, never stopping to take in the scenery.

# AV Gen — Rainbow World: Audio-Reactive Color, Radiance, and Spectral VFX Pass

## Mission

Perform a focused visual-effects pass on the existing Rainbow World scene.

**The primary VFX objective is expressive, audio-reactive color modulation.** The scene should become much more colorful, bright, shimmering, sparkling, radiant, pulsing, energetic, vibrant, and dynamic as it responds to music.

The current scene already has a promising visual identity: a camera flying rapidly around a twisting, folded 3D race-track-like world, surrounded by green terrain, blue sky, clouds, trees, rocks, flowers, and large rainbow arches. It has a playful, fantastical sense of speed and scale.

That identity is working. Preserve it.

The goal is not to redesign the world, make the terrain more physically distorted, or replace its current look with a generic neon environment. Instead, **make the existing world capable of expressing an extraordinary range of color, light, and surface animation**.

Think about what makes the most compelling rainbow-themed racing environments so visually engaging: vivid but deliberate color relationships, luminous surfaces, saturated environments, moving light, bright accents, animated patterns, and the sensation of racing through a world full of energy.

Use that as a broad point of inspiration, not a request to copy a specific game's assets, layouts, or visual design.

The default scene styling should remain recognizable. Audio and MIDI modulation should transform and enrich that styling rather than permanently converting everything into a rainbow.

## Creative direction

The desired visual qualities are:

- Colorful
- Bright
- Shimmering
- Sparkling
- Radiant
- Pulsing
- Energetic
- Vibrant
- Dynamic
- Spectral
- Luminous
- Playful
- Immersive
- Musically synchronized

The most important change in emphasis is this:

**Stop prioritizing distorted geometry as the main expression of audio reactivity. Prioritize color, emissive response, material animation, moving highlights, spectral transitions, light propagation, and localized visual effects.**

Geometry animation is still allowed where it serves a clear purpose, but it should not be the default solution to every audio-reactive opportunity.

We want to make the world itself feel like a colorful musical instrument.

---

# Phase 1 — Inspect the Existing Scene and Available Systems

Before implementation, inspect the actual repository and establish what the scene already supports.

## 1.1 Rainbow World

Locate the existing scene and study:

- Scene registration and configuration.
- Terrain, folded world geometry, camera path, and camera motion.
- Existing materials and material parameters.
- Rainbow geometry and materials.
- Sky, clouds, trees, rocks, grass, flowers, and other entities.
- Lighting, bloom, post-processing, and color-management settings.
- Existing scene-level and entity-level VFX.
- Existing audio and MIDI mappings.
- Live and offline scene configurations.
- Existing scene quality settings and profiling tools.

Render and inspect representative sections of the scene before changing it.

Document the current default colors and the most important material characteristics so the new effects preserve the visual foundation.

Do not infer how the scene is implemented from its appearance alone.

## 1.2 Investigate existing color modulation

The live scene reportedly supports changing color palettes through MIDI controls. Locate and understand that functionality.

Determine:

- Where the palettes are defined.
- Which entities and materials can use them.
- Whether palette changes are discrete or smoothly interpolated.
- Whether the existing implementation supports audio modulation.
- How palette controls are exposed to the signal bus and scene parameters.
- How MIDI control values are normalized, smoothed, and applied.
- Whether palette changes are already available to offline scenes.
- What can be reused without creating duplicate systems.

The goal is to extend and improve an existing capability, not create a second unrelated palette system.

Investigate whether palette selection, palette blending, and continuous material modulation can coexist. A MIDI control may select a broad visual direction while audio signals animate that palette continuously.

## 1.3 Inspect other AV Gen experiments

Investigate the existing resonator scenes and other experimental scenes for systems that could improve Rainbow World.

Look specifically for reusable implementations or ideas involving:

- Resonance-driven color or emissive modulation.
- Surface ripples and interference patterns.
- Animated scans, waves, or light bands.
- Shimmering and sparkling effects.
- Emissive materials and bloom.
- Spectral color mappings.
- Light beams and atmospheric effects.
- Procedural shader animation.
- MIDI-controlled palettes and modulation.
- Efficient instancing or other GPU-friendly effects.

Read the actual implementations and their associated documentation. Determine what is reusable, what would require adaptation, and what would be inappropriate for this scene.

Do not import a resonator scene wholesale. The goal is to reuse useful capabilities and visual techniques while retaining Rainbow World's identity.

## 1.4 Audit the rendering pipeline

Understand the actual color pipeline before implementing color effects.

Inspect:

- Material color representation and shader conventions.
- Color-space assumptions.
- Linear versus display-referred operations.
- HDR and emissive handling.
- Existing bloom implementation and controls.
- Tonemapping and output conversion.
- How shader parameters are updated.
- Whether related materials can share parameters efficiently.
- How post-processing affects saturation, highlight detail, and color separation.

Do not blindly apply hue rotation to final display-referred RGB values if a better material-space or perceptually appropriate approach is available.

Avoid creating a full-screen color-cycling effect that washes out the scene's material differences.

---

# Phase 2 — Research Colorful, Audio-Reactive Environments

Conduct focused research into techniques that can produce this aesthetic efficiently.

Use the following references as starting points, then investigate additional sources independently.

## 2.1 Rainbow Road and animated color surfaces

Study the visual principles of rainbow-themed racing environments, particularly the different ways color and light are animated.

Reference:

- Wii Rainbow Road and its animated track texture and moving points of light:  
  https://www.mariowiki.com/Wii_Rainbow_Road
- Rainbow Road variations and their use of multicolored surfaces:  
  https://www.mariowiki.com/Rainbow_Road
- Mario Kart 8 Rainbow Road and its color-shifting surfaces:  
  https://www.mariowiki.com/Rainbow_Road_%28Wii_U%29

Focus on the general principles:

- Animated patterns embedded in surfaces.
- Moving points or bands of light.
- Color transitions that preserve the underlying material.
- Strong color contrast against the environment.
- Selective use of rainbow surfaces rather than making everything rainbow at once.
- Spatial patterns that enhance a sense of movement and speed.

Do not copy recognizable game-specific geometry, track layouts, textures, or assets.

## 2.2 Emissive materials

Reference:

- Unreal Engine: Using the Emissive Material Input  
  https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-emissive-material-input
- Unreal Engine: Expanded Material Instances  
  https://dev.epicgames.com/documentation/unreal-engine/artist-04-expanded-material-instances?lang=en-US

Investigate the underlying principles of:

- Separating base color from emissive contribution.
- Controlling emission intensity independently from hue.
- Using masks to selectively illuminate parts of a surface.
- Combining lit materials with emissive details.
- Avoiding the loss of surface detail when emission becomes strong.

Use AV Gen's material and renderer architecture. Unreal is a reference for concepts, not a dependency.

## 2.3 Procedural shader animation

Reference:

- NVIDIA GPU Gems 2: Implementing Improved Perlin Noise  
  https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-26-implementing-improved-perlin-noise
- NVIDIA GPU Gems: Effective Water Simulation from Physical Models  
  https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models

Investigate procedural approaches to:

- Animated surface patterns.
- Multiscale shimmering.
- Traveling color bands.
- Soft, organic color variation.
- Ripple and interference patterns.
- Spatially coherent animation.
- Low-cost procedural detail.

Focus on shader-driven color and surface appearance rather than adding geometry or CPU-side updates unnecessarily.

## 2.4 Audio-reactive visual techniques

Reference:

- TouchDesigner audio-reactive GLSL tutorial:  
  https://derivative.ca/community-post/tutorial/touchdesigner-audio-reactive-visuals-glsl/73339
- TouchDesigner audio-reactive visual workflows:  
  https://derivative.ca/community-post/tutorial/audio-reactive-visuals-touchdesigner/65638
- Web-Onset spectral-flux onset-detection experiment:  
  https://github.com/Keavon/Web-Onset

Investigate how audio signals can control:

- Palette transitions.
- Emissive intensity.
- Color-band movement.
- Spectral patterns.
- Surface shimmer.
- Localized bursts of light.
- Continuous modulation and discrete accents.

Research smoothing, envelope followers, onset detection, temporal coherence, and perceptually meaningful mappings.

## 2.5 Additional independent research

Find additional high-quality references covering:

- Rainbow and iridescent shader techniques.
- HSV/HSL, OKLab, or other appropriate color interpolation methods.
- Spectral gradients and multi-stop palette interpolation.
- Animated caustics and light patterns.
- Glints, sparkles, and moving highlights.
- Bloom and HDR highlight management.
- Color grading for highly saturated environments.
- GPU instancing and efficient particle effects.
- Music-driven material animation and VJ systems.
- Real-time rendering performance and color stability.

For each relevant technique, record:

1. The source and URL.
2. The technique being investigated.
3. Why it is relevant to Rainbow World.
4. How it could be adapted to AV Gen.
5. Its likely performance implications.
6. Whether any source code or assets can legally be reused.

Do not spend the task merely gathering references. Research should lead directly to tested visual experiments.

---

# Phase 3 — Establish the Color Modulation Architecture

The scene needs a coherent way to express color modulation across many different materials.

First determine whether the existing parameter and modulation architecture can support this. Reuse it wherever practical.

## 3.1 Separate four related concepts

Keep the following concepts distinct where the current architecture permits:

### A. Base appearance

The existing appearance of the environment:

- Green terrain.
- Blue sky.
- Naturalistic or stylized foliage.
- Rocks and ground detail.
- Clouds.
- Existing rainbow structures.

This is the default visual identity and must remain attractive without audio reactivity.

### B. Palette identity

The broader color relationships that define a visual state.

Examples include a warm, sunset-like palette; a bright candy-colored palette; a cool violet and blue palette; or a vivid spectrum-rich palette.

These are coherent visual treatments, not arbitrary independent colors assigned to each object.

### C. Continuous modulation

Audio-driven variation in properties such as:

- Color blend.
- Saturation.
- Brightness.
- Emissive intensity.
- Animated pattern speed.
- Highlight strength.
- Spectral-band position.
- Shimmer intensity.

### D. Event-driven accents

Short-lived effects triggered by musical events:

- Brief color pulses.
- Moving light bands.
- Localized sparkles.
- Emissive bursts.
- Rainbow sweeps.
- Coordinated flashes across related objects.

Do not implement every event as a global palette change.

## 3.2 Prefer shared controls over unrelated per-object logic

Where appropriate, define a small set of reusable scene-level controls, such as:

- Palette blend or palette selection.
- Global spectral intensity.
- Color-motion speed.
- Shimmer amount.
- Emissive energy.
- Accent intensity.
- Surface-animation intensity.
- Environmental glow.

These are conceptual examples, not a mandate to add every parameter.

The exact control set should emerge from the existing architecture and the effects actually implemented.

Entities can interpret the same signal differently. A shared spectral-energy signal might influence terrain hue, foliage highlights, rainbow intensity, and cloud illumination through different mappings.

This is preferable to making every entity independently analyze the audio or apply the same raw hue shift.

## 3.3 Preserve material identity

Do not reduce every material to one uniform color.

The terrain must still read as terrain. Rocks should retain their surface character. Foliage should retain its shape and visual separation. Clouds should remain readable against the sky.

Use masks, procedural patterns, material-specific blending, and selective emission to preserve surface detail.

The objective is to let color move through the world without erasing the world's identity.

---

# Phase 4 — Build a Rich Palette System

Color should be the central expressive mechanism of this pass.

## 4.1 Develop a curated palette library

Study the existing palette implementation and extend it rather than replacing it without reason.

Create a curated collection of distinct, visually coherent palettes.

Potential directions to explore include:

- The existing default green, blue, and rainbow palette.
- Candy-bright pink, coral, violet, and golden yellow.
- Electric purple, magenta, and deep blue.
- Warm gold, orange, coral, and pink.
- Vivid spectral colors against a cooler environment.
- Bright spring green, cyan-blue, and violet accents.
- Sunset-inspired orange, rose, and lavender.
- High-energy combinations using red, indigo, and magenta.

These are starting directions. Evaluate the actual rendered results and retain only palettes that work.

Avoid allowing every palette to collapse into the same cyan/magenta look. Use a wider range of hue relationships and make each palette meaningfully distinct.

Do not change the default palette merely to prove that the new system works.

## 4.2 Smooth palette transitions

Investigate interpolation between palettes so that transitions can feel musical and fluid.

Requirements:

- No abrupt hue discontinuities during ordinary transitions.
- No unexpected gray or muddy intermediate colors.
- No sudden changes in exposure.
- No loss of contrast between terrain, sky, and foreground objects.
- No frame-rate-dependent transition behavior.
- Stable output under the existing color pipeline.

Evaluate interpolation in an appropriate color space. Do not assume that interpolating RGB components independently always produces the best perceptual result.

If the existing MIDI palette controls are discrete, determine whether the transition behavior can be improved without breaking the established interaction.

## 4.3 Explore multi-palette blending

In addition to selecting a palette, explore blending between a small number of curated color states.

For example, a scene might retain its base palette while a second palette gradually influences selected materials, with a third accent palette reserved for strong events.

The transition should be controllable and bounded.

Do not create a combinatorial explosion of palettes or a large configuration system that makes the scene difficult to maintain.

## 4.4 Audio-driven palette evolution

Use audio features to influence palette evolution.

Possible behaviors:

- Bass energy increases the intensity of selected terrain colors.
- Drum accents create brief palette pulses.
- Sustained synth energy slowly shifts the scene toward another palette.
- Spectral brightness influences selected highlight colors.
- Combined musical activity increases spectral richness.
- Strong musical transitions move the scene toward a different color state.

These are examples, not fixed mappings. Choose the actual behavior after studying the audio and existing analysis features.

**The goal is not to cycle through the entire hue spectrum continuously.** The goal is to make palette movement feel composed, varied, and responsive to the music.

---

# Phase 5 — Apply Color VFX Across the Entire Environment

Every major material category should be capable of participating in the color system, but each should express it differently.

## 5.1 Terrain and grass

Prioritize the large terrain surfaces because they occupy much of the image and strongly influence the overall visual mood.

Explore:

- Smooth audio-reactive shifts between selected terrain palettes.
- Fine, moving color patterns across the grass.
- Traveling bands of spectral illumination.
- Localized color pulses following strong musical events.
- Small emissive details that appear and fade.
- Shimmering highlights that move across the surface.
- Subtle multiscale procedural color variation.
- Color waves that travel along the folded world.

Keep the existing green terrain as a strong base state.

Do not turn the entire ground into a uniformly emissive rainbow texture. Selective effects, localized patterns, and controlled palette blends should create visual richness without destroying the landscape.

Avoid unnecessary terrain displacement. The main visual change should come from material appearance and lighting.

## 5.2 Rocks and geological details

Explore:

- Subtle color migration across rock surfaces.
- Emissive seams, veins, or selected edge details.
- Brief colored glints.
- Moving highlights.
- Small spectral patterns that become visible at stronger musical moments.
- Localized pulses that illuminate a cluster of rocks.

Preserve the existing rock materials and silhouettes.

Do not make every rock glow constantly. Use selective masks and intensity ranges so that rocks can become colorful without losing their underlying material character.

## 5.3 Trees, leaves, and vegetation

Explore:

- Audio-reactive shifts in foliage highlights.
- Subtle spectral coloration at the leaf or canopy level.
- Shimmering points of light.
- Brief color pulses across selected foliage.
- Moving highlights that follow a coherent direction.
- Small bursts of colored particles where they contribute meaningfully.

Keep the trees readable as trees.

Avoid applying the same hue shift to every channel of every foliage material. Use different controls for base foliage, highlights, and emissive accents where supported.

## 5.4 Clouds and sky

The sky offers a large visual area in which color can evolve without requiring new geometry.

Explore:

- Gradual sky-color transitions.
- Subtle color variation across clouds.
- Audio-driven illumination of cloud edges.
- Soft, traveling color gradients.
- Brief spectral accents in selected cloud regions.
- Light shafts or atmospheric glow during stronger musical moments.

Maintain the existing sky's visual character. Avoid constant full-screen rainbow gradients that flatten the scene or compete with the terrain.

The sky should participate in the performance, sometimes subtly and sometimes dramatically.

## 5.5 Rainbow arches

Treat the existing rainbow arches as the scene's most obvious spectral hero elements.

Explore:

- Moving color bands along the arches.
- Traveling highlights.
- Animated spectral gradients.
- Pulses that propagate along the geometry.
- Selective emissive intensification.
- Subtle internal shimmer.
- Coordinated responses with nearby terrain and sky.
- Bright accents that align with meaningful musical events.

The rainbow structures should be able to look beautiful in their default state and spectacular when the music activates them.

Do not simply rotate the rainbow's hue continuously. Use spatially structured animation and controlled brightness changes.

## 5.6 Smaller objects and environmental details

Inspect the scene for additional surfaces that could contribute:

- Flowers.
- Grass accents.
- Small decorative objects.
- Ground patterns.
- Existing particles.
- Other visible environmental elements.

Give these objects subtle, complementary responses.

Do not force every object to become a primary focal point. Visual hierarchy matters.

---

# Phase 6 — Build the Color VFX Library

Create a compact set of high-quality, reusable effects.

The following are candidate effects. Prototype them and retain those that work best with the existing scene.

## 6.1 Spectral flow

A moving band of color that travels along a surface, rainbow arch, terrain region, or another suitable world feature.

It should have:

- A controllable direction and speed.
- A controllable width and intensity.
- Smooth entry and exit.
- A defined palette.
- A trigger or continuous control.
- A clear spatial relationship to the world.

Use world-space coordinates where appropriate so the effect remains attached to the environment as the camera moves.

## 6.2 Shimmer

A layered, animated highlight or color variation that makes surfaces feel luminous and alive.

Explore combining:

- Slow, broad variation.
- Faster, fine-grained variation.
- Selective highlights.
- Audio-driven intensity.
- Spatially coherent motion.

Avoid random per-frame flicker. The effect should feel like light moving across a surface, not noise being generated independently on every frame.

## 6.3 Sparkle and glints

Create small, visually controlled accents that respond to musical events.

Possible forms include:

- Short-lived surface glints.
- Tiny luminous points.
- Brief streaks.
- Small particle bursts.
- Localized highlights on vegetation or rocks.

Use event strength and musical context to control intensity and density.

Do not flood the screen with particles or use a generic particle explosion for every event.

## 6.4 Color pulses

Create smooth, bounded pulses of color or emissive intensity.

Possible targets include:

- Terrain patches.
- Rainbow arches.
- Foliage clusters.
- Rock formations.
- Clouds.
- Other visible environmental features.

Use short envelopes for transients and longer envelopes for sustained musical energy.

## 6.5 Traveling light waves

Create color or brightness waves that move across surfaces or between related world features.

These can provide a sense of movement without deforming the geometry.

Investigate inexpensive shader-based implementations first.

## 6.6 Emissive accents

Use emissive color selectively to make specific elements radiate.

Separate the desired color from the intensity so that audio can increase brightness without forcing an unrelated hue change.

Test the result with the existing bloom and tonemapping pipeline.

Do not assume that increasing emissive intensity will always improve the image. Preserve detail in bright surfaces and avoid large areas of featureless white.

## 6.7 Optional resonator-inspired effects

Investigate whether the resonator experiments provide useful color and surface-animation techniques.

Potential adaptations include:

- Layered interference patterns.
- Concentric or traveling color ripples.
- Animated scan lines.
- Resonance-like spectral pulses.
- Luminous wave patterns across terrain or rainbow structures.

Use these only if they fit the scene's visual language.

Do not introduce an unrelated geometric resonator effect merely because the implementation exists elsewhere.

---

# Phase 7 — Map Audio to Color

Use the actual stem-analysis and modulation architecture in AV Gen.

If Feline Footwear's stems are available for this scene, use them to validate the mappings. If not, make sure the scene remains functional with live audio or another supported source.

The precise effect mapping must be chosen based on actual audio analysis, not generic assumptions about instrument roles.

## 7.1 Drums

Use drum transients and rhythmic density to drive quick visual accents.

Potential responses:

- Short color pulses.
- Brief emissive boosts.
- Traveling spectral highlights.
- Sparkles or glints.
- Localized rainbow accents.
- Coordinated color impacts across nearby surfaces.

Differentiate strong hits from smaller events.

Avoid triggering a full-scene palette change on every drum transient.

## 7.2 Bass

Use bass energy to control broader and more sustained color behavior.

Potential responses:

- Terrain saturation or color intensity.
- Gradual palette blending.
- Slow-moving spectral patterns.
- Broad illumination waves.
- Sustained emissive accents.
- Large-scale environmental color energy.

Do not rely on geometric deformation as the primary bass response.

## 7.3 Synth

Use synth features to create more fluid, expressive, and atmospheric color behavior.

Potential responses:

- Slow palette transitions.
- Traveling color bands.
- Rainbow animation.
- Cloud and sky illumination.
- Shimmering highlights.
- Evolving spectral patterns.
- Phrase-level color swells.

Select the actual behavior based on the synth stem's musical character.

## 7.4 Combined-stem behavior

When multiple stems reinforce one another, coordinate their visual effects.

For example, a drum accent might trigger a short spectral pulse while sustained bass energy keeps the terrain luminous and the synth drives a moving color band through the rainbow arches.

Use this as a design pattern, not a predetermined mapping.

The resulting scene should have several simultaneous layers of activity without making every layer respond identically.

## 7.5 Preserve contrast and musical phrasing

Keep the scene visually alive between transients, but allow meaningful changes in intensity.

A useful hierarchy is:

- Continuous low-level shimmer and color evolution.
- Regular rhythmic accents.
- Stronger event-driven spectral effects.
- More substantial combined-stem moments.
- Rare, visually spectacular phrase-level changes.

Do not maximize every effect continuously.

---

# Phase 8 — Integrate with Existing MIDI and Audio Modulation

Investigate how the current live MIDI palette controls can coexist with audio-driven color.

The intended result is a coherent modulation system, not competing sources that constantly overwrite one another.

Where supported, consider a clear hierarchy:

- The selected base palette establishes the scene's color identity.
- MIDI controls provide intentional artistic direction.
- Audio signals drive continuous variation and event-driven accents.
- Scene-level parameters determine intensity and limits.

Determine whether the existing architecture already supports modulation layering and parameter ownership. Do not add a second control system unnecessarily.

Verify that MIDI controls remain useful in live performance.

Where a MIDI palette selection changes the broad visual direction, audio should continue animating the result rather than becoming disabled or immediately replacing the selected palette.

Test both manual control and automatic modulation.

---

# Phase 9 — Iterative Visual Development

Use a short, deliberate visual-development loop.

Do not implement the entire effect library before seeing the results.

## Milestone A: Color foundation

Implement and evaluate:

1. A robust palette-transition system using existing infrastructure where possible.
2. One terrain color-modulation effect.
3. One moving spectral effect on the rainbow arches.
4. One selective emissive or shimmer effect.
5. Audio-driven control of at least some of these effects.

Render the scene and inspect the results.

Verify that the default state remains attractive and that the new effects genuinely enrich the scene.

## Milestone B: Environmental coverage

Extend the best effects to:

- Rocks.
- Foliage.
- Clouds and sky.
- Flowers and smaller environmental elements.

Do not blindly reuse the same shader treatment on every material. Adapt the effect to each material's visual role.

## Milestone C: Musical composition

Add:

- Drum-triggered color pulses.
- Bass-driven sustained color energy.
- Synth-driven spectral movement.
- Coordinated effects across related world elements.
- More substantial moments when the musical arrangement warrants them.

Review actual playback and adjust the timing and intensity.

## Milestone D: Polish

Refine:

- Color relationships.
- Transition smoothness.
- Shimmer speed and scale.
- Emissive intensity.
- Bloom behavior.
- Effect density.
- Spatial coherence.
- Performance.

Remove effects that do not improve the scene.

The target is not the largest possible effect library. It is a smaller set of excellent effects that work together.

---

# Phase 10 — Performance and Live Use

The scene must remain suitable for live audiovisual performance.

Inspect the current performance baseline and existing quality controls before implementing expensive effects.

Prefer:

- Shader-based material animation.
- Shared modulation signals.
- Efficient instancing.
- Selective rather than universal emissive effects.
- Reuse of existing bloom and post-processing.
- Localized effects instead of unnecessary full-screen passes.
- Bounded particle counts.
- Frame-rate-independent animation.

Avoid:

- CPU updates to every vertex or object when a shader can do the job.
- Per-object FFTs or duplicated analysis.
- Unnecessary additional render passes.
- Excessive transparent overdraw.
- Unbounded particle spawning.
- Expensive full-screen color processing without demonstrated value.

Profile the scene with all new effects active at the intended live/projector resolution.

Preserve the established 30 FPS live target under the tested configuration. Record the actual hardware, resolution, quality settings, and CPU/GPU timings.

Offline rendering may support additional quality where the architecture permits, but it should preserve the same underlying visual behavior and musical mapping.

---

# Phase 11 — Acceptance Criteria

The pass is complete when the following are true.

### Color and visual identity

- The existing default look remains recognizable.
- Palette modulation is a major and expressive part of the scene.
- The environment can transition between meaningfully different color states.
- Terrain, rocks, trees, sky, clouds, and rainbow structures all support suitable color VFX.
- Shimmering, sparkling, and emissive accents are spatially coherent.
- The scene has substantially more visual energy without relying on major geometry distortion.
- Color effects preserve material and environmental readability.
- Strong effects create moments of spectacle without overwhelming the entire scene continuously.

### Musical response

- Color changes are tied to useful audio signals rather than arbitrary random cycling.
- Drum transients create fast, legible accents.
- Bass creates sustained or broad color energy where appropriate.
- Synth drives fluid, atmospheric, or spectral behavior appropriate to the actual audio.
- Combined events create coordinated effects.
- Transitions are smooth and stable.
- MIDI palette controls remain useful and coexist with audio modulation.

### Technical quality

- Existing AV Gen architecture is reused where appropriate.
- The renderer's color pipeline is respected.
- No unnecessary CPU-side geometry distortion is introduced.
- Performance is measured at the intended live configuration.
- The scene meets the established live frame-rate target under the documented test conditions.
- Offline behavior remains deterministic where supported.
- The scene remains functional when the showcase stem files are unavailable.

### Deliverables

Produce:

1. A concise summary of the existing palette and modulation implementation.
2. An annotated research dossier.
3. Documentation of the final palette and audio-to-color mapping.
4. The integrated scene implementation.
5. Representative stills and short clips demonstrating the changes.
6. A performance comparison against the baseline.
7. Relevant tests and usage instructions.

---

# Working Principles

- Preserve the existing scene's identity.
- Prioritize color, light, and surface animation over geometry distortion.
- Do not turn every object into a permanent rainbow.
- Do not rely on indiscriminate hue rotation.
- Do not make every material react in the same way.
- Do not equate greater brightness with greater visual quality.
- Avoid continuous full-screen color cycling.
- Research and inspect existing implementations before building replacements.
- Test effects in motion, against actual audio, and in the real renderer.
- Prefer a few polished, composable systems over a large collection of unrelated effects.
- Keep live performance and future reuse in mind.

## Final creative objective

When the camera races through Rainbow World, the landscape should feel like it is continually discovering new ways to express color.

The hills can shimmer with moving spectral detail. Rocks can catch brief luminous accents. Foliage can glow with subtle, shifting highlights. Clouds can respond to musical swells. Rainbow arches can transmit waves of color and energy. The entire environment can move between beautiful palette states while remaining recognizable as the same world.

The scene should not look like a static landscape with a rainbow filter applied.

**It should feel like a vivid, radiant, colorful world whose materials, atmosphere, and light are performing the music.**
