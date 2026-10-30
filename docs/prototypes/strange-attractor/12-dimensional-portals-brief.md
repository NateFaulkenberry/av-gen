# AV Gen — Strange Attractor: Matrix Transition, Dimensional Portals & Lunar Environment

> Owner brief, verbatim, 2026-10-30. Coordinator notes for the agent are in
> `13-portals-coordinator-notes.md`; where they differ, the owner's words here win.

## Objective

Develop a substantial visual art pass for the existing Strange Attractor scene.

The current scene already has a beautiful sandstone canyon, reflective river, surrounding terrain, and an interesting triangle-tracing geometry scan effect.

Preserve and build upon this foundation.

The new direction is to transform Strange Attractor into a journey between dimensions:

1. Begin in the existing natural canyon environment.
2. Follow the river through the canyon.
3. Gradually transform the canyon geometry into an increasingly complex computational visualization (the "Matrix effect").
4. Approach a dimensional portal embedded in the canyon wall.
5. Pass through the portal into a completely different environment: the surface of the Moon.
6. Explore the lunar environment and approach a second portal located inside a crater.
7. Pass through that portal and return to the original canyon.
8. The canyon is now at dusk, suggesting that a significant amount of time has passed.
9. Continue the camera journey along the river.

This is primarily an offline visual-quality art pass. The existing 1080p/30fps live-rendering target is a soft constraint, not a reason to compromise the visual ambition of this sequence.

## Phase 1 — Audit the existing implementation

Before changing anything:

- Inspect the complete Strange Attractor scene implementation.
- Identify the camera path, animation timeline, terrain generation, canyon geometry, river, materials, lighting, and existing triangle-tracing effect.
- Determine how the current geometry scan is implemented and which parts are reusable.
- Inspect existing data-moshing effects and any existing geometry-displacement, procedural material, particle, post-processing, and temporal effects.
- Investigate the engine's capabilities for rendering multiple environments, scene transitions, render-to-texture, clipping, depth, and portal-like effects.
- Examine existing scene architecture and determine how best to implement multiple environments without creating an unnecessarily complicated system.

Produce a short implementation plan before making changes.

Do not redesign the existing canyon or replace its underlying terrain simply to accommodate the new effects.

## Phase 2 — Develop the Matrix effect

This is the most important artistic component of the assignment.

The existing triangle-tracing effect is a good starting point. Retain its underlying visual language, but evolve it into a much richer computational transformation.

The goal is to create the sensation of transitioning from a normal physical environment into a digital/computational reality.

### 2.1 Outer geometry scanning

Preserve the current triangle-tracing effect and improve its visual quality.

Desired characteristics:

- Scanning reveals the topology of the surrounding canyon geometry.
- The effect should follow the actual terrain rather than appearing as an arbitrary overlay.
- Triangle lines should have depth, perspective, and varying visual intensity.
- Introduce traveling scan fronts that reveal portions of the terrain progressively.
- Experiment with different line densities and localized concentrations.
- Use copper, amber, bronze, and gold as the initial palette.
- Allow the scan effect to become more prominent as the camera approaches the portal.

The existing effect should feel like an environmental scanning system.

### 2.2 Inner computational breakdown

Introduce a second, more chaotic visual layer.

As the camera approaches the portal, the scanned geometry should begin breaking down.

Explore:

- Data-moshing and temporal displacement.
- Horizontal and vertical image tearing.
- Fragmented rectangular data blocks.
- Displaced geometry strips.
- Localized vertex displacement.
- Triangles breaking apart into smaller fragments.
- Surface reconstruction and temporary geometry disappearance.
- Temporal echoes and frame displacement.
- Cascading digital fragments and streams of computational data.
- Spatial distortion that appears to pull surrounding geometry toward a central focal point.

Look at the visual language of the Matrix films, particularly their digital code visualization, as inspiration.

Also investigate our existing AV Gen data-moshing effects and reuse or extend those systems where appropriate.

Do not simply apply a full-screen glitch filter. The effect needs to interact with the actual three-dimensional environment.

The objective is a combination of:
- Real geometry visualization.
- Procedural surface corruption.
- Spatial data distortion.
- Image-space data-moshing.

Experiment with these layers together.

### 2.3 Increasing visual intensity

The effect needs a deliberate progression.

Far from the portal:
- Mostly natural terrain.
- Occasional geometry scans.
- Subtle computational artifacts.

Approaching the portal:
- More frequent scanning.
- Increasing triangle density.
- More visible data corruption.
- Greater displacement and fragmentation.

Immediately surrounding the portal:
- Heavy computational distortion.
- Rapidly moving data fragments.
- Geometry appears to separate and fold.
- Stronger temporal and spatial corruption.
- A sense that the surrounding environment is being consumed by the portal.

Avoid making the entire environment uniformly noisy. Preserve recognizable terrain wherever possible so the transformation remains legible.

The visual progression should feel intentional and artistically controlled.

## Phase 3 — First dimensional portal

Create a portal embedded in or emerging from a canyon wall.

The portal should be a genuine dimensional opening rather than a conventional glowing ring.

### Portal appearance

Explore an irregular elliptical or vertically oriented aperture.

The surrounding canyon geometry should appear to be separating, folding, or being pulled apart.

The Matrix effect should converge around the portal.

Potential visual elements:
- Distorted terrain around the aperture.
- Geometry fragments being pulled inward.
- Computational scanning lines converging on the opening.
- Distorted spatial boundaries.
- A luminous but controlled perimeter.
- An actual view into the lunar environment through the aperture.

Use Portal and Portal 2 as technical and conceptual references for dimensional portals, particularly the principle that the destination environment is visible through the opening.

Do not copy their art direction.

### Portal functionality

The portal must reveal the actual lunar environment.

As the camera approaches:
- The lunar environment should become visible through the aperture.
- Perspective should make the destination feel like a real space beyond the opening.
- The camera should move through the portal plane.
- The transition should feel continuous.
- Avoid a simple fade-to-black, crossfade, or obvious scene cut.

Investigate the best implementation for AV Gen's renderer.

Possible approaches include:
- Portal surfaces displaying a separately rendered environment.
- Render-to-texture with appropriate camera projection.
- A controlled scene transition using matched camera trajectories.
- Geometry clipping and depth-aware compositing where supported.

Choose the approach that provides the best visual result without introducing unnecessary architectural complexity.

## Phase 4 — Build the lunar environment

Create a completely different environment representing the surface of the Moon.

This should be a meaningful environmental contrast to the existing canyon.

### Environment design

Desired visual characteristics:

- Vast lunar terrain.
- Large impact craters.
- Gray, dusty, rocky surfaces.
- Interesting geological formations.
- Black space overhead.
- Potential distant Earth visibility if it improves the composition.
- Strong directional sunlight.
- Long, dramatic shadows.
- A convincing sense of scale.

The environment should feel barren, expansive, and alien.

Avoid generic science-fiction scenery, excessive futuristic structures, or artificial-looking procedural terrain.

The Moon should feel like a physical environment.

### Lunar camera journey

After passing through the first portal:

- Continue the camera movement into the lunar environment.
- Establish the landscape visually before immediately approaching the second portal.
- Travel across the lunar surface.
- Introduce a substantial impact crater.
- Locate the second portal within that crater.

The lunar sequence should have its own visual identity and lighting.

## Phase 5 — Second portal and return to the canyon

Create another dimensional portal inside a lunar crater.

This portal returns us to the original canyon.

### Portal appearance

The second portal should be visually related to the first, but does not need to be identical.

It should be integrated into the lunar crater rather than appearing to float arbitrarily above the surface.

The portal should reveal the canyon environment through its opening.

Ideally, we can see the sandstone formations and river beyond it.

### Return journey

The camera approaches and passes through the lunar portal.

We emerge back into the original canyon.

However, this is no longer the same time of day.

## Phase 6 — Canyon at dusk

The returned canyon should clearly communicate that time has passed.

Reuse the existing canyon terrain and river rather than building another canyon.

Develop an alternate dusk lighting and atmosphere configuration.

Desired characteristics:

- Deep violet and indigo sky.
- Warm residual sunlight on the sandstone formations.
- Longer and deeper shadows.
- More dramatic contrast.
- Changed river reflections.
- Subtle atmospheric depth.
- A more subdued, contemplative appearance than the original daytime environment.

This should still unmistakably be the original canyon.

The camera should emerge from the portal and continue its journey along the river.

Do not end the sequence immediately after returning. Establish the transformed environment and allow the camera to continue moving.

## Phase 7 — Camera choreography and sequencing

Integrate the new environments and effects into a coherent camera journey.

The existing opening camera movement should be retained wherever possible.

Proposed sequence:

| Sequence | Approximate timing |
|---|---|
| Existing canyon descent | 0–8 seconds |
| River approach | 8–16 seconds |
| Matrix effect develops | 16–24 seconds |
| Portal formation | 24–30 seconds |
| First portal crossing | 30–35 seconds |
| Lunar exploration | 35–43 seconds |
| Crater portal formation | 43–49 seconds |
| Return to canyon | 49–54 seconds |
| Dusk canyon continuation | 54–60 seconds |

These timings are suggestions, not rigid requirements.

Prioritize good camera composition, readable transitions, and visual pacing.

Avoid abrupt camera acceleration or movements that make it difficult to understand where the camera is traveling.

The portal crossings should feel like the culmination of the camera journey rather than arbitrary scene transitions.

## Phase 8 — Artistic references and research

Before settling on implementation details, investigate useful technical and artistic references.

Particularly:

1. Matrix digital rain and computational environment visualization techniques.
2. Data-moshing and temporal image corruption techniques.
3. Portal and Portal 2 dimensional portal rendering and compositing.
4. Real-time and offline rendering techniques for portals between environments.
5. Procedural geometry distortion and surface reconstruction.
6. Cinematic lunar landscape references.

Look for actual tutorials, implementation breakdowns, shader examples, and technical demonstrations where useful.

Use these references to develop techniques appropriate to AV Gen.

Do not reproduce existing copyrighted artwork or simply recreate a movie or game effect. The objective is to borrow useful visual techniques and develop our own artistic interpretation.

## Implementation requirements

- Work within the existing Strange Attractor scene architecture.
- Reuse existing AV Gen effects where appropriate.
- Prefer extending existing systems over creating redundant alternatives.
- Do not sacrifice the existing canyon's visual quality.
- Avoid unnecessary backwards-compatibility layers.
- Do not introduce generic procedural sci-fi assets simply to fill out the lunar environment.
- Prioritize visual quality, composition, and convincing transitions over live performance optimization.
- Keep the implementation controllable and maintainable.
- Expose useful parameters for artistic iteration where appropriate.
- Ensure the sequence can be rendered consistently offline.
- Test the complete camera journey, not just isolated screenshots of the effects.

## Definition of done

The art pass is successful when:

1. The opening canyon remains as visually appealing as the existing version.
2. The triangle-tracing effect has been significantly refined.
3. The Matrix effect develops from recognizable geometry scanning into increasingly complex computational corruption.
4. The computational effect interacts with the actual three-dimensional environment.
5. The first portal reveals a convincing lunar environment.
6. The camera visibly and convincingly passes through the portal.
7. The lunar environment has a distinct and convincing visual identity.
8. A second portal opens within a lunar crater.
9. The camera returns through that portal into the original canyon.
10. The canyon now looks like dusk, creating a convincing sense of temporal displacement.
11. The camera continues along the river after returning.
12. The entire sequence works as one coherent, visually compelling offline scene.

## Final priority

The most important thing is the Matrix effect and the dimensional transitions.

I would rather have two exceptionally well-executed portals, a beautiful lunar environment, and a genuinely impressive computational transformation than a large number of secondary effects.

Treat this as a VFX artist's environment and sequence-building exercise, not simply a programming task.

Build something visually ambitious, cohesive, and memorable.
