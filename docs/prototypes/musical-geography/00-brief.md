# AV Gen Scene Development Specification: Procedural Geometric Terrain / Musical Geography

## 1. Project Objective

Develop a new, production-quality audiovisual scene for AV Gen built around an enormous, continuous, procedurally generated landscape composed of uniformly distributed square terrain tiles.

The initial visual concept is inspired by the supplied reference image (`IMG_4270.PNG`), which originated from an earlier character animation / intelligent pathfinding experiment in AV Gen.

The reference demonstrates an interesting square-based representation of terrain, with individual tiles occupying different elevations and visual states.

We want to evolve this basic concept into a visually sophisticated, continuously animated audiovisual environment.

The central artistic idea:

**An enormous, living geometric landscape whose terrain, color, illumination, and movement respond to music.**

Imagine an environment consisting of millions of potential square positions, with rolling hills, valleys, water-like expanses, elevated plateaus, winding pathways, and enormous traveling waves.

The landscape should feel organic and fluid despite being constructed entirely from discrete geometric elements.

The camera continuously explores this environment, flying over and through its geographical formations. There are no camera cuts or discrete scene transitions.

We may also introduce stylized humanoid characters that explore the terrain, run across platforms, jump between tiles, and interact physically with the floating landscape.

This should become a distinctive AV Gen audiovisual experience, not a technical demonstration of procedural terrain.

---

# 2. Mandatory Initial Investigation

Before implementing anything, investigate the existing AV Gen codebase.

This is particularly important because the original square-terrain experiment was generated within AV Gen during earlier character animation and pathfinding work.

We want to recover and reuse as much of that functionality as possible.

### Investigate:

1. Locate the original terrain/pathfinding experiment.
2. Identify the scene, prototype, branch, commit, or relevant source files associated with it.
3. Determine how the original square distribution is generated.
4. Identify existing terrain elevation and deformation functionality.
5. Identify any existing character navigation, pathfinding, collision, or terrain-query systems.
6. Determine whether the original implementation uses instancing or individual scene objects.
7. Identify existing animation and material systems that can be reused.
8. Investigate how the experiment interacts with AV Gen's audio modulation architecture.
9. Identify existing camera controllers and cinematic evaluation infrastructure.
10. Review existing scene implementations for reusable techniques.

Inspect existing ADRs and architecture documentation where relevant.

Do not assume the original prototype is production-ready. It may be a debugging visualization or an experimental representation of a navigation mesh.

Determine which aspects are worth preserving and which should be replaced.

### Important

Do not introduce duplicate systems if AV Gen already provides suitable infrastructure.

Do not create an entirely separate terrain engine or character animation framework.

Use the existing scene architecture and established engine conventions.

Before implementation, produce a concise technical plan covering:

- Existing functionality discovered.
- Functionality to reuse.
- New functionality required.
- Proposed scene architecture.
- Rendering and performance considerations.
- Implementation phases.
- Testing strategy.

Use independent investigation subagents where beneficial, particularly for the original prototype, rendering architecture, and character systems.

Do not begin implementation until the investigation and plan are complete.

---

# 3. Artistic Direction

## Overall aesthetic

The visual identity should be:

- Geometric.
- Organic.
- Colorful.
- Atmospheric.
- Surreal.
- Cinematic.
- Contemporary.

The environment should feel like an abstract geographical world that exists somewhere between a physical landscape and a mathematical visualization.

The geometry should be relatively simple, but the overall presentation should be sophisticated.

We do not need enormous amounts of conventional environmental detail.

The primary visual interest comes from the relationship between:

- Tile geometry.
- Elevation.
- Terrain formations.
- Color.
- Lighting.
- Movement.
- Atmospheric perspective.
- Camera composition.

Avoid the appearance of a conventional computer-generated grid visualization.

The reference image is a starting point for the underlying geometry, not a target for its muted color palette or limited terrain variation.

### Explicit artistic restrictions

Avoid:

- Uniformly flat terrain.
- Randomly generated noise as the sole source of elevation.
- Generic neon grids.
- Excessive cyan/teal coloration.
- Excessively dark or muddy materials.
- Minecraft-like block landscapes.
- Repetitive procedural hills.
- Conventional low-poly environmental scenery.
- Unnecessary trees, buildings, rocks, and decorative assets.
- Excessive particle effects.
- Busy visual overlays.
- Generic sci-fi HUD elements.

The squares themselves must remain the primary visual identity.

The result should look intentionally art-directed rather than randomly procedurally generated.

---

# 4. Terrain Geometry and Distribution

This is the foundation of the entire scene.

Create a large, continuous terrain system constructed from uniformly distributed square tiles.

### Tile requirements

- Consistent tile dimensions.
- Consistent spacing.
- Deterministic grid positioning.
- No unintended overlapping geometry.
- No gaps caused by incorrect positioning or deformation.
- Individually controllable elevation.
- Individually controllable material properties.
- Support for emissive coloration and edge illumination.

The grid should maintain its underlying spatial integrity regardless of terrain deformation.

Tiles should not drift horizontally or lose their alignment during animation.

Small, intentional gaps between tiles are acceptable and potentially desirable because they help establish their individual silhouettes.

Explore slightly beveled edges or subtle chamfers if they improve the visual presentation.

Avoid excessive beveling that makes the tiles resemble manufactured furniture or building blocks.

### Terrain formations

The landscape should contain several different geographical characteristics that can coexist within one continuous environment.

#### A. Water-like expanses

Broad, relatively flat areas with gentle, flowing elevation changes.

These should visually suggest water without literally rendering a conventional water surface.

Possible characteristics:

- Gentle traveling waves.
- Subtle elevation changes.
- Blue, turquoise, cobalt, indigo, and occasional contrasting colors.
- Smooth transitions between neighboring tiles.
- Reflective or glossy material experiments where appropriate.

#### B. Rolling hills

Larger geographical formations with smooth elevation changes.

These should feel like actual rolling terrain rather than isolated piles of raised squares.

Use coherent spatial functions to create hills, ridges, depressions, and valleys.

#### C. Canyons and valleys

More dramatic elevation changes.

Create long, winding depressions that cut through elevated terrain.

These can be visually emphasized through material transitions, shadows, and contrasting illumination.

#### D. Elevated plateaus

Broad, relatively flat areas at varying elevations.

These are particularly useful for character movement and can serve as geographical landmarks.

#### E. Winding pathways

Create recognizable paths that travel through the landscape.

These can be represented through changes in tile coloration, elevation, illumination, or combinations of all three.

Paths should integrate naturally into the environment.

### Terrain generation principles

Do not rely exclusively on random per-tile height values.

Use combinations of coherent spatial functions and procedural geographical masks to produce formations that read as intentional terrain.

Support blending between different terrain characteristics.

The system should be capable of generating large, visually coherent environments without obvious repeating patterns.

---

# 5. Dynamic Terrain Animation

This is one of the most important features of the scene.

The landscape should be continuously animated, even without audio input.

We want multiple independent deformation systems that can operate simultaneously.

### Required animation behaviors

**Traveling waves**

Large wave formations travel across the terrain.

These should affect groups of tiles rather than individual isolated tiles.

Wave crests and troughs should create recognizable geographical formations.

**Radial disturbances**

Localized disturbances propagate outward from a central position.

These could be triggered by audio transients, character landings, or other events.

**Terrain breathing**

Large geographical regions slowly rise and fall.

This should be subtle enough to avoid making the entire environment look like a malfunctioning grid.

**Rolling formations**

Hills, ridges, and valleys slowly migrate across the landscape.

**Frequency-driven displacement**

Different frequency bands can influence separate deformation systems.

For example:

- Low frequencies influence broad geographical elevation changes.
- Mid frequencies influence secondary terrain movement and rolling formations.
- High frequencies influence smaller, more localized tile movement.

**Transient-driven displacement**

Audio transients can trigger localized pulses or temporary tile elevation changes.

Do not assume every transient is a kick drum.

### Wave interference

Explore combining multiple traveling wave systems.

For example, two wave formations traveling in different directions should be capable of intersecting and producing more complex elevation patterns.

The resulting terrain should feel organic and mathematically interesting.

### Implementation requirements

- All deformation must be time-continuous.
- Avoid abrupt discontinuities between tiles.
- Neighboring tile elevations should be capable of forming coherent surfaces.
- Preserve deterministic terrain queries.
- Make deformation parameters configurable.
- Ensure that character navigation and collision use the same effective terrain surface as rendering.

Prefer GPU-side deformation where appropriate.

Avoid unnecessary CPU updates for every individual tile.

The deformation architecture should support both autonomous animation and audio-driven modulation.

---

# 6. Tile Materials and Visual Effects

The individual tiles should provide considerable visual variety without requiring elaborate geometry.

Implement a flexible material system supporting:

### Surface treatments

- Smooth matte surfaces.
- Subtle glossy surfaces.
- Gradient coloration.
- Height-dependent coloration.
- Directional color gradients.
- Selective emissive surfaces.
- Optional translucent or glass-like regions.

### Edge treatments

Experiment with:

- Thin illuminated tile outlines.
- Edge tracing animations.
- Independently controlled edge emission.
- Highlighted tile boundaries.
- Traveling illumination along tile edges.

One particularly interesting effect would be a wavefront of illumination traveling across the terrain, following the crest of a moving wave.

### Selective illumination

Do not illuminate every tile equally.

Create large regions of subdued coloration with strategically illuminated areas.

Use selective highlights to establish geographical formations and direct visual attention.

Tile illumination should be controllable independently from surface coloration.

### Audio interaction

Individual tiles or geographical regions should support modulation from:

- Frequency bands.
- Audio energy.
- Transients.
- Modulation buses.
- Timeline-driven parameters.

Do not implement a collection of hardcoded audio-to-visual mappings.

Use AV Gen's existing modulation architecture.

---

# 7. Global Color and Atmospheric Environment

Implement a global environment and color-palette system.

The entire world should be capable of transitioning between distinct visual environments without interrupting camera movement or changing scenes.

Explore at least four palette states.

### Daylight

- Cobalt blue.
- Turquoise.
- Lavender.
- Pale yellow.
- Warm peach.

Bright, relatively soft illumination.

### Sunset

- Orange.
- Coral.
- Crimson.
- Magenta.
- Deep violet.

More dramatic directional illumination and longer shadows.

### Night

- Deep indigo.
- Electric blue.
- Violet.
- Selective pink highlights.

Emissive tile edges become more prominent.

### Dawn

- Golden amber.
- Pale yellow.
- Warm cream.
- Soft greens.

Gradually increasing environmental illumination.

### Environment controls

The global palette controller should influence:

- Sky coloration.
- Atmospheric scattering.
- Ambient illumination.
- Directional light coloration.
- Terrain base colors.
- Tile gradients.
- Emissive intensity.
- Edge illumination.
- Fog coloration and density.
- Shadow characteristics.

Transitions should be smooth and continuous.

Do not simply swap material presets.

### Audio integration

The global palette controller must operate independently from audio modulation.

The world might transition from sunset to night over several minutes while audio energy simultaneously influences terrain warmth, emissive intensity, and localized coloration.

The result should feel like a cohesive audiovisual environment.

---

# 8. Character Simulation and Interaction

Characters are an important secondary feature.

However, the landscape must be visually successful without them.

Build the terrain system first, then integrate character simulation.

### Character requirements

Investigate existing AV Gen character assets, animation clips, navigation, and skeletal animation infrastructure.

Reuse existing assets where appropriate.

Characters should have distinctive, stylized silhouettes.

Avoid generic game NPC aesthetics.

Potential behaviors:

- Walking across the terrain.
- Running along winding paths.
- Jumping between elevated tiles.
- Exploring the environment.
- Gathering on plateaus.
- Occasionally stopping and observing their surroundings.

Characters should feel like inhabitants exploring a strange geographical world.

They should not be excessively large relative to the landscape.

The environment remains the protagonist.

### Navigation

Characters must navigate the actual deformed terrain.

They should not simply follow predetermined horizontal paths that ignore elevation.

Account for:

- Tile elevation.
- Neighboring tile accessibility.
- Slope limitations.
- Jumpable gaps.
- Landing locations.
- Terrain changes during movement.

Navigation should remain stable when terrain deformation is active.

### Floating platform physics

This is a particularly important artistic feature.

Tiles should behave somewhat like floating platforms resting on a fluid surface.

When a character lands on a tile:

1. The tile depresses slightly.
2. It rebounds upward.
3. It oscillates briefly.
4. The oscillation gradually settles.
5. Neighboring tiles can receive smaller disturbances.

The response should be smooth and spring-like.

Characters should also have subtle floating movement when idle.

During jumps, their movement should have a slightly exaggerated, buoyant quality.

### Interaction propagation

A character landing should optionally produce a disturbance that propagates into surrounding tiles.

This could create a small ripple traveling outward through the landscape.

Use the same underlying terrain deformation infrastructure wherever possible.

Avoid implementing a completely separate physics system just for these interactions.

### Scope control

Do not allow character implementation to derail the primary environment work.

Get the terrain, lighting, camera, and audio-driven movement working first.

Then introduce a small number of characters and evaluate their visual contribution.

Do not add dozens of characters merely to make the environment appear populated.

---

# 9. Continuous Cinematic Camera

This scene must use a single continuously moving camera.

**Absolutely no cuts, scene changes, teleportation, or discontinuous camera transitions.**

The camera should behave like a cinematic drone exploring an enormous geographical environment.

### Camera behaviors

Implement a continuous camera controller capable of combining:

**Low-altitude flight**

Travel relatively close to the terrain, revealing individual tile geometry and elevation.

**High-altitude flight**

Climb to reveal the enormous scale of the landscape and its geographical formations.

**Terrain following**

Maintain a relatively consistent distance above the terrain while following its contours.

**Sweeping turns**

Gradually change direction to reveal new geographical formations.

**Slow descent**

Approach interesting terrain features or characters before continuing onward.

**Cinematic banking**

Introduce subtle banking during turns and directional changes.

### Camera composition

Avoid monotonous straight-line movement.

The camera should continuously explore different terrain characteristics.

Occasionally use broad, sweeping movements that reveal the scale of the environment.

Other times, move closer to the terrain to emphasize its geometry.

Use appropriate depth perception, atmospheric perspective, and carefully considered focal length.

The camera should feel intentional rather than randomly animated.

### Continuity

Maintain a continuous world coordinate system.

The environment must be sufficiently large or procedurally extended to prevent obvious boundaries.

If terrain streaming or chunk generation is necessary, ensure that transitions are seamless.

Avoid visible popping, regeneration artifacts, or sudden changes in geographical characteristics.

The camera controller should support autonomous movement without requiring user input.

---

# 10. Audio Integration

The scene must function as a genuine live audiovisual instrument.

Use AV Gen's established audio analysis, buses, modulation, timeline, and parameter infrastructure.

Do not create a parallel audio-analysis system.

### Suggested modulation assignments

| Audio characteristic | Visual response |
|---|---|
| Low-frequency energy | Broad terrain elevation |
| Mid-frequency energy | Rolling formations and secondary displacement |
| High-frequency energy | Localized tile movement and edge illumination |
| Transients | Radial disturbances and localized pulses |
| Overall energy | Global terrain activity and illumination |
| Musical progression | Camera behavior and environmental palette evolution |

These are starting suggestions, not mandatory hardcoded assignments.

All mappings should be configurable.

### Important audio considerations

Do not rely exclusively on inferred kick, snare, and hi-hat classifiers.

Where suitable multitrack stems are available, support independent modulation from individual stems.

Allow different audio sources to control different geographical and material behaviors.

The scene must also remain visually interesting when audio is quiet or when no useful transient information is available.

Autonomous animation should provide a continuously evolving baseline.

Audio should enhance and influence the world rather than being responsible for every movement.

---

# 11. Rendering and Performance

This is an AV Gen live scene, not an offline-only cinematic.

Target:

- 1920 × 1080.
- 30 FPS minimum operating target.
- Stable frame pacing.
- No noticeable visual stuttering.
- No excessive CPU-side geometry updates.

The scene should remain viable on the existing AV Gen rendering architecture.

### Technical priorities

1. Use instanced rendering for terrain tiles.
2. Avoid individual scene-object overhead for large tile populations.
3. Prefer GPU-side deformation where practical.
4. Avoid redundant CPU-side terrain calculations.
5. Keep character simulation proportional to the actual number of characters.
6. Use efficient terrain queries.
7. Avoid excessive dynamic shadow-casting lights.
8. Use appropriate culling and level-of-detail strategies.
9. Avoid unnecessary transparency and overdraw.
10. Preserve stable camera and terrain behavior across large world coordinates.

Do not rewrite the renderer or introduce a separate graphics backend.

Work within the existing WebGPU/Dawn architecture.

Do not compromise the artistic direction simply to maximize tile count.

A smaller, beautifully rendered landscape is preferable to an enormous, visually monotonous one.

### Performance evaluation

Use actual profiling rather than estimates.

Measure:

- CPU frame time.
- GPU frame time.
- Terrain rendering cost.
- Deformation cost.
- Character simulation cost.
- Shadow rendering cost.
- Memory consumption.
- Frame-time stability.

Test at 1080p.

Where practical, profile both the M2 Max development environment and the available lower-end Windows GPU environment.

---

# 12. Scene Development and Evaluation Process

Follow AV Gen's established development practices.

Use test-driven development for new systems.

### Phase 1: Discovery and planning

- Locate the original experiment.
- Audit reusable infrastructure.
- Review existing scene architecture.
- Produce the implementation plan.

### Phase 2: Terrain foundation

Implement:

- Uniform tile distribution.
- Stable tile indexing.
- Terrain elevation.
- Geographical formations.
- Coherent terrain deformation.
- Basic material variation.

At the end of this phase, the scene should already look like a compelling geometric landscape.

### Phase 3: Dynamic terrain

Implement:

- Traveling waves.
- Radial disturbances.
- Terrain breathing.
- Wave interference.
- Audio-driven deformation.
- Configurable modulation parameters.

### Phase 4: Visual identity

Implement:

- Material treatments.
- Color gradients.
- Edge illumination.
- Tile-specific effects.
- Global environment controller.
- Day/night transitions.
- Atmospheric lighting.

### Phase 5: Camera

Implement the autonomous continuous camera.

Establish a visually compelling exploration path through the environment.

The camera must not cut or teleport.

### Phase 6: Characters

Integrate character assets, navigation, jumping, landing, and floating platform interactions.

Prioritize a small number of high-quality character interactions.

### Phase 7: Audio refinement

Connect the scene to the existing AV Gen modulation system.

Test with actual music and available stems.

Evaluate the scene across materially different musical characteristics, not just one track.

### Phase 8: Cinematic evaluation and optimization

Use AV Gen's existing cinematic evaluation workflow.

This is mandatory.

Do not substitute a basic screenshot review for cinematic evaluation.

Evaluate:

- Overall visual identity.
- Terrain variation.
- Camera movement.
- Color composition.
- Visual hierarchy.
- Character integration.
- Animation quality.
- Audio responsiveness.
- Repetitiveness.
- Performance.

Capture representative frames and evaluate the scene as a continuous audiovisual experience.

Identify weaknesses and make iterative improvements.

Do not consider the scene complete simply because it compiles and renders.

---

# 13. Testing Requirements

Write automated tests for new deterministic functionality.

At minimum, test:

### Terrain

- Tile positions are deterministic.
- Tile distribution is uniform.
- Tiles do not unintentionally overlap.
- Tile spacing remains consistent.
- Terrain deformation produces finite values.
- Neighboring tile positions remain coherent.
- Terrain queries agree with the deformation model.

### Animation

- Wave functions are continuous.
- Multiple deformation sources combine correctly.
- Radial disturbances decay appropriately.
- Tile landing responses settle correctly.
- Time advancement produces deterministic results where expected.

### Navigation

- Characters can navigate accessible terrain.
- Invalid jumps are rejected.
- Valid landing positions are identified.
- Terrain queries account for deformation.
- Character grounding remains stable.

### Environment

- Palette transitions interpolate correctly.
- Environment parameters remain within valid ranges.
- Audio modulation does not produce invalid material or lighting values.

### Performance

Where appropriate, introduce representative benchmarks for terrain generation, terrain queries, deformation, and rendering submission.

Do not create fragile tests that depend on exact rendered pixels when deterministic system-level tests are more appropriate.

Run the relevant existing test suites and ensure no regressions are introduced.

---

# 14. Definition of Done

The scene is not complete until all of the following are satisfied.

**Visual environment**
- A large, continuous square-tile landscape exists.
- Tile distribution is uniform and stable.
- Multiple distinct geographical formations coexist.
- Elevation changes create coherent terrain.
- The environment has substantial color variation.
- The scene does not look like a generic grid visualization.

**Animation**
- Terrain is continuously animated.
- Multiple wave systems can coexist.
- Audio influences terrain movement.
- Tile illumination responds to modulation.
- The environment evolves even during quiet audio passages.

**Atmosphere**
- Global palettes transition smoothly.
- Day/night environmental changes work.
- Lighting and atmospheric perspective establish geographical depth.

**Camera**
- A single continuous camera explores the environment.
- No cuts or teleportation occur.
- Camera movement is cinematic and compositionally interesting.
- World boundaries are not visible.

**Characters**
- Characters can navigate the terrain.
- Jumping and landing interactions work.
- Floating platform responses are visually convincing.
- Characters complement rather than overwhelm the landscape.

**Engineering**
- Existing AV Gen architecture is respected.
- New systems have appropriate automated tests.
- No unnecessary duplicate infrastructure is introduced.
- The scene runs at the intended live performance target.
- No unrelated renderer or engine rewrites are introduced.

**Evaluation**
- The existing cinematic evaluation workflow has been used.
- Multiple representative musical passages have been tested.
- Visual shortcomings have been addressed.
- Performance has been measured rather than assumed.
- Documentation and relevant scene artifacts have been updated.

---

# 15. Final Artistic Direction

The ultimate objective is to create something that feels like exploring a vast, musical geographical world.

A possible moment in the finished scene:

The camera begins relatively close to a broad, gently undulating blue landscape.

In the distance, enormous formations of elevated tiles begin rising and falling in slow waves.

The camera gradually gains altitude, revealing a winding canyon cutting through a series of colorful plateaus.

The terrain becomes more active as the music develops.

A series of illuminated wavefronts travels across the landscape. Their intersections create complex patterns of elevation and color.

Several small characters are visible running across the terrain. One jumps onto a raised platform, which depresses and rebounds, sending a subtle ripple into the surrounding tiles.

The camera banks and follows a wave across the landscape.

The environment gradually transitions from a warm sunset palette into a deep violet night, with selected tile edges becoming illuminated.

The camera continues onward, revealing another enormous geographical formation.

No cuts. No conventional scene transitions. Just one continuous exploration of an evolving world.

The scene should be visually compelling from both close-up and distant perspectives.

It should reward sustained viewing and feel genuinely responsive to music.

**The squares are the landscape. The landscape is the instrument. The camera is exploring it.**

Prioritize artistic quality, coherent motion, strong composition, and visual variety over feature count.

Build something distinctive.
