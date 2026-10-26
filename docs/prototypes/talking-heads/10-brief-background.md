# Talking Heads: the owner's background brief (verbatim, 2026-10-25)

The owner's covering note: "more background detail needed for talking heads, heres a spec:"

Coordinator notes:
- The eye beams were cut by the owner earlier; where this brief mentions eye beams, read mouth rays.
- It builds on the stop-4 atmosphere (mist, motes, sky drift), which it supersedes where they conflict.
- The brief asks for two parallel subagents; the owner's 2-agent cap is full (Aurora Borealis is running), so the Talking Heads agent covers both investigations itself.

---

# Talking Heads — Background Art Direction and Implementation

## Objective

Transform the background of the existing Talking Heads AV Gen scene into a much more visually dynamic, strange, surreal, and abstract environment.

The current scene features mannequin busts floating against a smooth purple gradient with sparse particles. The heads and their colored mouth/eye rays are working as the primary visual concept, but the surrounding environment feels empty and generic.

**The goal is to make the background feel like an active, surreal audiovisual environment that the heads inhabit, rather than a static gradient with decorative stars.**

This is an art-direction and implementation task, not a request to redesign the heads, replace their models, or rebuild the scene from scratch.

Inspect the existing scene and its rendering capabilities before making changes. Reuse the current scene architecture and prioritize visually distinctive results within the established 1080p/30 FPS live-rendering budget.

## 1. Artistic direction: a surreal signal dimension

Develop a background that feels like an abstract dimension composed of color, light, spatial distortion, and unstable visual structures.

It should evoke the feeling of an experimental electronic music video: strange, hypnotic, slightly unsettling, colorful, and continuously evolving.

The environment does not need to represent a literal place. It should feel spatially deep, but its forms can be abstract and dreamlike.

The visual language should combine:

- Large, slowly moving fields of saturated color.
- Distorted spatial planes or ribbons.
- Unusual geometric or organic silhouettes.
- Thin luminous structures extending through space.
- Localized ripples and waves of distortion.
- Occasional bursts of visual activity synchronized to the music.
- Strong contrasts between calm atmospheric passages and more intense moments.

Avoid simply filling the background with more objects. Every element should contribute to the composition, create depth, or provide a useful response to the music.

## 2. Primary background feature: warped spatial planes

Create one or two large, abstract spatial surfaces behind the heads.

These could resemble enormous flexible sheets, translucent membranes, distorted grids, or flowing topographic surfaces suspended in darkness.

They should have recognizable depth and shape, rather than looking like flat decorative overlays.

Possible visual behaviors:

- Broad waves travel across the surfaces.
- The surfaces bend or ripple in response to bass energy.
- Color gradients flow across them.
- Portions of the surface briefly illuminate on transients.
- The geometry develops slow, evolving deformations.
- Thin luminous lines or contours reveal the shape of the surface.

The surfaces should be irregular and visually interesting without becoming dense, noisy meshes.

Prefer a simple mesh with shader-based displacement or another inexpensive existing effect. Avoid expensive simulation.

The background surfaces should remain behind the heads and must not obscure the faces or overwhelm the mouth and eye beams.

## 3. Secondary feature: abstract spatial structures

Introduce a small number of unusual structures that establish depth and scale.

Possible forms include:

- Huge curved bands passing through the distant background.
- Thin, bent luminous frames.
- Discontinuous arcs or partial rings.
- Floating sheets with irregular silhouettes.
- Distorted parallel lines that bend toward and away from the viewer.
- Repeated contours that gradually shift in scale or orientation.

These structures should feel like fragments of a strange visual system, not recognizable architecture.

Do not build rooms, corridors, cathedrals, buildings, or elaborate mechanical installations.

Avoid generic sci-fi wireframe tunnels and the familiar screensaver look of endless rotating geometric objects.

Use asymmetry and carefully controlled placement. A few unusual silhouettes are preferable to dozens of generic shapes.

Animate these structures slowly, with selective transient-driven movement.

## 4. Color fields and background illumination

The current gradient is a useful starting point, but it should evolve spatially and temporally.

Develop large, overlapping regions of colored illumination that create the impression of an environment changing around the heads.

Use a controlled palette built around:

- Deep violet and indigo.
- Saturated magenta and pink.
- Electric blue.
- Occasional coral, red, or warm amber accents.
- Rare pale highlights.

Color should move across the environment in broad, coherent patterns.

Possible behaviors:

- Slow color waves traveling across the background.
- A broad magenta field swelling behind the central composition.
- Blue and violet regions drifting past one another.
- Color bands responding to sustained musical energy.
- Brief changes in saturation or hue on strong transients.
- Localized illumination appearing behind an active head.

Do not make the entire background cycle through the rainbow continuously. Establish a recognizable base palette, then introduce deliberate transitions and accents.

Color modulation should be one of the main visual effects, not an incidental material animation.

## 5. Strange atmospheric phenomena

Introduce a small number of effects that make the space feel unstable or uncanny.

Select the most visually effective options supported by the current renderer.

### A. Spatial ripples

Large, slow-moving rings or ripples travel through the environment, distorting nearby lines, surfaces, or color fields.

They could originate behind a head or from a point in the distant background when a strong transient occurs.

Avoid conventional circular audio-visualizer rings. The ripples should interact with the spatial composition and feel like disturbances moving through a strange medium.

### B. Light filaments

Thin, luminous strands stretch through portions of the background.

They may curve, branch, undulate, or briefly illuminate as sound intensity changes.

Keep them sparse. They should create unusual silhouettes and guide the eye toward the heads without competing with the primary mouth and eye rays.

### C. Background signal distortion

Apply occasional, restrained spatial or chromatic disturbances to the environment.

Examples include brief horizontal offsets, shifting contours, or a wave of displaced color.

Prefer localized effects or inexpensive shader implementations over expensive full-screen processing.

The effect should feel like the environment itself is malfunctioning.

### D. Floating visual fragments

Use a small number of carefully designed particles or fragments that travel through depth.

Vary their size, distance, and motion. Some may briefly illuminate or react to nearby sound activity.

Do not fill the scene with uniform stars or generic particle dust.

These are optional supporting elements. Prioritize the warped surfaces, spatial composition, and color fields first.

## 6. Relationship between the background and the heads

The heads remain the main subject.

The background should create contrast, depth, and context for their beams.

Use the following compositional principles:

- Keep the immediate region around each face relatively readable.
- Allow large color fields to form halos or areas of contrast behind the heads.
- Place abstract structures at different depths.
- Let some light filaments pass behind the busts, never confusing them with the mouth and eye emitters.
- Preserve negative space around the main subjects.
- Avoid bright background details directly behind the most important facial features.
- Maintain visual separation between the heads and the environment.

The background may become more intense during energetic passages, but the heads should remain identifiable.

Do not solve visual emptiness by simply increasing brightness, adding more stars, or increasing the number of objects.

## 7. Audio reactivity

Use the existing AV Gen signal-routing and modulation systems.

Do not introduce a new audio-analysis pipeline.

Create distinct behaviors for different types of musical energy.

| Signal | Suggested background response |
|---|---|
| Low-frequency energy | Large surface waves and slow spatial deformation |
| Bass transients | Brief ripple propagation or structural displacement |
| Mid-frequency energy | Movement of color fields and illumination |
| High-frequency energy | Fine filament activity and small localized distortions |
| Strong transients | Short bursts of spatial or chromatic disturbance |
| Sustained energy | Gradual illumination and evolving surface motion |
| Quiet passages | Slow, subtle movement and restrained color transitions |

These mappings are starting points. Tune them according to the existing scene's signals and the musical results.

Use smooth attack/release behavior where appropriate. Avoid constant jitter, arbitrary random movement, or effects that remain at maximum intensity throughout a track.

Some background animation should evolve independently of the audio so the environment never feels completely frozen during quiet passages.

The music should influence the environment's behavior without making every object move on every beat.

## 8. Motion and temporal composition

The background needs multiple layers of motion operating at different timescales.

### Slow motion

Large surfaces deform gently. Color fields migrate across the scene. Distant structures shift their orientation or apparent position.

These movements establish the scene's baseline atmosphere.

### Medium-speed motion

Filaments undulate, contours shift, and localized regions of illumination change over musical phrases.

These elements provide continuous visual interest without becoming distracting.

### Fast motion

Strong transients trigger short bursts of ripples, color changes, or structural distortion.

Fast effects should be occasional and purposeful rather than constant.

Avoid making every background element rotate, pulse, and oscillate independently. The environment should feel like a coherent system with related movements.

## 9. Rendering and performance constraints

Maintain the established target of 1080p/30 FPS.

Before implementing anything, inspect which of the following are already supported:

- Procedural or shader-displaced meshes.
- Existing material parameter modulation.
- GPU particles or effect fields.
- Beam and ribbon geometry.
- Bloom or glow.
- Existing screen-space distortion.
- Depth-aware composition and post-processing.

Reuse the existing capabilities wherever possible.

Prioritize a small number of large, visually distinctive surfaces and structures over a high object count.

Avoid:

- Expensive fluid simulations.
- Large numbers of transparent overlapping layers.
- Unnecessary real-time shadow casting.
- High-resolution volumetric effects.
- New rendering passes without a clear visual justification.
- Full-screen data moshing as a prerequisite.
- Replacing the existing renderer or scene architecture.

Measure CPU and GPU performance before and after implementation. Capture the scene at its intended resolution.

If a feature is expensive, first explore a simpler representation that preserves its visual character.

## 10. Implementation workflow

1. Inspect the existing Talking Heads scene and its current appearance.
2. Identify the background implementation, available effects, and reusable rendering features.
3. Produce a short art and implementation plan.
4. Select two or three background features that create the greatest visual improvement.
5. Implement those features before adding secondary details.
6. Integrate audio-reactive controls through the existing systems.
7. Build and run the scene.
8. Capture visual evidence at 1080p.
9. Review the result for composition, visual hierarchy, motion quality, and performance.
10. Refine the background based on the actual rendered output.

Where supported, use two parallel subagents: one to investigate feasible visual references and background treatments, and one to audit the engine's existing rendering and effect capabilities. Reconcile their findings before implementation.

Do not stop at a technically correct but visually mediocre first pass. Inspect the rendered scene and make targeted artistic corrections.

## 11. Acceptance criteria

The work is complete when:

- The background has a recognizable visual identity beyond a gradient and particles.
- Large-scale forms create depth and spatial interest.
- Color evolves in broad, deliberate patterns.
- At least two distinct background phenomena respond meaningfully to audio.
- Background motion operates at multiple timescales.
- The heads and their emitted beams remain the focal point.
- The scene feels strange, abstract, hypnotic, and slightly unsettling.
- The result does not resemble a generic screensaver, sci-fi tunnel, or random geometric demo.
- The scene remains within its measured live-performance budget.
- Visual captures demonstrate both a quiet passage and an energetic passage.

## Final artistic test

If the heads were temporarily hidden, the background should still look like an intentional piece of abstract audiovisual art.

When the heads are visible, the background should make them feel as though they are emitting signals into, disturbing, or communicating with a larger environment.

The desired result is not a busy backdrop. It is a surreal space with its own visual behavior and identity, designed specifically to complement the Talking Heads concept.
