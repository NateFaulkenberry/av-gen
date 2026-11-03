# The owner's brief, verbatim (2026-11-03)

Given with this instruction: "before we do a full m3g video here's an update spec for another art pass, you can
divide this amongst two sub agents how you see fit, be sure to preserve all of what we have now in terms of portal
animations, camera paths, general visual ideas - dont introduce any regressions in the scenery".
The coordinator's split is in `56-environment-art-pass-coordination.md`.

---

# AV Gen — Strange Attractor: Environment Art Direction, Cinematic Polish & World-Building

## Mission

Perform a comprehensive environment-art polish pass across the Strange Attractor scene.

The objective is to elevate the visual quality of the existing environments through thoughtful environment art, geological detail, sophisticated materials, terrain treatments, atmospheric effects, lighting, and cinematic presentation.

We are NOT looking to make every environment photorealistic or fill every available space with geometry.

We want environments that feel professionally designed, handcrafted, visually cohesive, and convincing at multiple camera distances.

The artistic direction is:

**Handcrafted cinematic stylized realism.**

Think high-quality environment art from a professionally produced science-fiction film or a carefully art-directed modern game, rather than generic procedural landscapes or conventional photorealistic asset dumps.

We already have a good foundation. Preserve the elements that are working and concentrate effort where the environments currently look unfinished.

This is an offline-oriented scene. Do not constrain artistic decisions around the normal live-rendering performance budget. Higher-resolution textures, additional geometry, displacement, volumetrics, expensive lighting, and more sophisticated rendering techniques are acceptable.

Blender is available for substantial environment and material authoring.

Most importantly, use our EXISTING CINEMATIC EVALUATION WORKFLOW throughout the project.

---

# 1. Existing Scene Assessment

The current Strange Attractor environments include:

1. Canyon (day) — opening canyon landscape.
2. Moon — lunar surface, Earth, and illuminated geometric crater structures.
3. Mars — desert landscape with pyramids and ancient structures.
4. Underwater — submerged environment with a central structure.
5. Canyon (dusk) — transition toward the character encounter location.
6. Canyon (night) — campfire encounter with sasquatches, alien, and UFO.

The current artistic direction is already reasonably established.

We like:

- The large-scale geological forms in the canyon.
- The existing canyon color palette.
- The sculptural quality of the landscape.
- The moon's relatively clean, graphic composition.
- The geometric structures embedded in the lunar craters.
- The contrast between natural environments and unusual science-fiction elements.
- The overall sense of traveling through different environments.

We do NOT want to discard these characteristics in pursuit of generic photorealism.

## Initial priorities

### Canyon (day)
Preserve the existing composition and geological identity. Use it as the primary environment-art benchmark.

Improvements should be relatively restrained and focused on geological refinement, materials, surface variation, and atmospheric depth.

### Moon
Preserve the existing visual identity and composition.

Only introduce improvements where they meaningfully contribute to the environment. Avoid unnecessarily increasing detail.

### Mars
This environment needs substantial attention.

The current landscape feels too flat and uniform. It needs more distinctive geology, more convincing terrain materials, better atmospheric depth, and stronger integration of the pyramids into their surroundings.

### Underwater
This environment also needs substantial attention.

The current seabed feels relatively generic and the environment lacks convincing underwater depth. Improve the geological formations, sediment, water column, lighting, and integration of the central structure.

### Canyon (dusk/night)
Treat this as a dedicated hero environment rather than simply reusing the broad canyon landscape.

The character encounter location needs to work particularly well at ground level and during closer cinematic shots.

Preserve the overall canyon setting, but give the campfire location more intentional environmental design.

---

# 2. FIRST REQUIREMENT: Audit and Use the Existing Cinematic Evaluation System

Before implementing any artistic changes, inspect the existing AV Gen Cinematic Critic / Cinematic Evaluation infrastructure.

This is a mandatory part of the assignment.

We already have an established cinematic evaluation workflow. Do not create a new evaluation framework, replace the existing one, bypass it, or invent a different scoring process.

First determine:

- How the cinematic evaluation is invoked.
- Which scene and camera configurations it uses.
- How reference renders are generated.
- What visual criteria it evaluates.
- What reports, scores, images, or artifacts it produces.
- How the results are intended to be interpreted.
- How to run it against Strange Attractor.
- Whether it supports evaluating individual cameras or scene sections.
- Whether there are existing baseline evaluations we can use.

Read the relevant implementation, documentation, scripts, and agent instructions.

Do not assume the evaluation interface or invocation method. Discover and use the actual existing workflow.

## 2.1 Establish baseline evaluations

Before changing anything, run the existing cinematic evaluation on the current Strange Attractor scene.

Capture the existing results for all six environment sections.

Preserve the original camera positions, camera settings, rendering configurations, and evaluation settings wherever possible.

We need a baseline that can be compared directly against subsequent iterations.

If the evaluator supports multiple camera positions or evaluation viewpoints, use them to assess both establishing shots and closer environmental compositions.

If some existing camera positions do not adequately expose the environmental shortcomings, add supplementary evaluation cameras without replacing the original ones.

## 2.2 Use the evaluation throughout implementation

The intended development loop is:

1. Modify or improve an environment.
2. Render the actual AV Gen scene.
3. Run the existing Cinematic Critic.
4. Inspect the evaluation results and actual rendered images.
5. Identify the most important visual weaknesses.
6. Implement targeted corrections.
7. Render again.
8. Re-run the cinematic evaluation.
9. Compare against the previous iteration.

Repeat until the visual improvements are convincing.

Do not run the evaluation only once at the end.

Do not blindly optimize for a numerical score. The actual rendered images and artistic quality remain the ultimate criteria.

If the evaluator identifies an issue that is already an intentional artistic choice, use judgment rather than blindly changing it.

Likewise, do not ignore recurring evaluation failures simply because the scene looks acceptable from one camera.

## 2.3 Evaluation separation

Keep cinematic quality evaluation separate from performance optimization.

This is an offline-oriented visual-art task.

Do not run unnecessary live-performance optimization or sacrifice visual quality to improve a live-performance benchmark.

Use the existing cinematic evaluation as the primary feedback mechanism.

Do not modify the evaluation system itself unless a genuine integration problem prevents its use. Any necessary changes must be minimal, documented, and preserve existing functionality.

---

# 3. Establish an Environment Art Direction

Before adding detail, inspect the existing environments and establish a coherent artistic language.

The goal is not to make every location look identical.

Each environment should retain its own distinctive visual identity while sharing an underlying level of artistic sophistication.

Our guiding principles:

- Large-scale environmental forms should dominate composition.
- Medium-scale details should reinforce the geology and sense of place.
- Small-scale details should provide texture and visual richness without becoming noise.
- Materials should have meaningful variation.
- Lighting should reinforce the environment's character.
- Every major environmental feature should feel deliberately placed or naturally formed.
- Avoid obvious procedural repetition.
- Avoid unnecessary asset proliferation.

We want environments that feel authored, not randomly populated.

Produce a concise environment-art direction document describing:

- Overall visual language.
- Color and material direction.
- Geological design principles.
- Environmental detail distribution.
- Atmospheric and lighting principles.
- Common material/shader techniques.
- Differences in treatment between the six environments.

---

# 4. Environment Detail: Work at Three Scales

Do not approach environmental detail as simply increasing polygon counts or texture resolution.

Work at three distinct scales.

## 4.1 Macro detail — geological composition

This includes:

- Cliff silhouettes.
- Canyon walls.
- Mountains.
- Craters.
- Large rock formations.
- Mesas.
- Geological landmarks.
- Terrain elevation.
- Large environmental structures.

Inspect existing geometry and identify areas that look too uniform, artificial, repetitive, or flat.

Potential improvements:

- More interesting geological silhouettes.
- Irregular erosion patterns.
- Natural fractures.
- Overhangs and ledges.
- Exposed rock formations.
- Large boulders.
- Distinctive environmental landmarks.
- Better transitions between terrain types.

Prioritize shape and composition over indiscriminate geometric density.

An environment should have recognizable landmarks and interesting silhouettes.

Avoid making every cliff, mountain, or formation look like the same procedural asset with different scaling.

## 4.2 Medium detail — environmental structure

This is likely the most important opportunity for improving the current environments.

Investigate:

- Rock clusters.
- Broken geological formations.
- Erosion channels.
- Fractured ledges.
- Sediment deposits.
- Smaller boulders.
- Terrain undulations.
- Cracks and fissures.
- Deposited material.
- Distinctive rock groupings.

Use clustered detail rather than uniform random distribution.

For example, in the canyon:

- Larger rocks should accumulate naturally near cliff bases.
- Fractured stone should appear in believable geological groupings.
- Sediment should collect in appropriate low areas.
- Some terrain sections should remain relatively clean.
- Distinctive formations should be placed intentionally.

Create visual rhythm through variation and negative space.

Avoid scattering hundreds of identical rocks uniformly across the environment.

## 4.3 Micro detail — material and surface quality

Investigate:

- High-quality tiling textures.
- Normal maps.
- Roughness maps.
- Displacement.
- Fine surface imperfections.
- World-space procedural variation.
- Mineral deposits.
- Weathering.
- Surface cracks.
- Geological strata.
- Fine sediment detail.

Use detail appropriate to each material.

A sandstone cliff should not have the same microstructure as lunar regolith or underwater rock.

Avoid excessive high-frequency noise.

The goal is believable surface response to light, not visual noise.

---

# 5. Material and Shader Development

Audit the existing environment materials before authoring replacements.

Develop a reusable library of sophisticated environmental material techniques.

These should be adaptable to different environments without imposing a single universal appearance.

## 5.1 Geological material system

Develop a reusable geological material approach supporting:

- Multiple geological layers.
- World-space color variation.
- Fine surface noise.
- Normal-map detail.
- Roughness variation.
- Subtle displacement.
- Mineral deposits.
- Weathering.
- Surface imperfections.

For the canyon, prioritize convincing sandstone.

The material should suggest geological strata, weathering, and erosion rather than simply applying a repeating orange rock texture.

## 5.2 Sediment and ground materials

Develop appropriate treatments for:

- Fine desert dust.
- Sandstone sediment.
- Loose rock.
- Lunar regolith.
- Martian dust.
- Underwater sediment.

Consider:

- Fine granular normal detail.
- Directional wind patterns where appropriate.
- Subtle displacement.
- Roughness variation.
- Blended surface materials.
- Natural color variation.

Avoid obvious tiling.

## 5.3 World-space material variation

Investigate world-space procedural breakup as a way to reduce repetition.

Use broad-scale variation and micro-detail together.

For example, a sandstone wall might contain:

- Broad geological color bands.
- Medium-scale erosion variation.
- Fine surface grain.
- Localized roughness differences.

Avoid relying entirely on one high-frequency noise function.

## 5.4 Common material quality

All environments should benefit from:

- Consistent PBR material interpretation.
- Correct normal-map handling.
- Appropriate roughness response.
- Good texture filtering.
- High-quality texture resolution.
- Natural transitions between material types.
- Appropriate interaction with scene lighting.

Validate all material work in the actual AV Gen renderer.

Blender preview quality alone is not sufficient.

Do not assume that Blender node configurations will transfer directly into our renderer.

---

# 6. Environment-Specific Improvements

## 6.1 Canyon (day) — benchmark environment

Preserve the existing landscape and composition.

This environment should establish the quality standard for the project.

Priorities:

**Geological refinement**
- Improve irregularity in cliff silhouettes.
- Introduce occasional fractured sections.
- Improve erosion channels.
- Add some distinctive rock formations.
- Improve transitions between geological layers.

**Materials**
- Improve sandstone strata.
- Introduce subtle color variation.
- Add normal-map detail.
- Improve roughness variation.
- Introduce localized mineral/weathering details.

**Terrain**
- Improve the canyon-floor surface.
- Introduce natural rock deposits near cliff bases.
- Add some scattered sediment and fractured stones.
- Avoid making the floor uniformly rocky.

**Atmosphere**
- Improve distant terrain separation.
- Introduce subtle atmospheric perspective.
- Improve sunlight/shadow distribution.
- Preserve the existing warm canyon palette.

Do not redesign the entire canyon.

The goal is to make an already successful environment feel more sophisticated.

## 6.2 Moon — restrained polish

Preserve the existing composition.

The black sky, Earth, lunar surface, and geometric structures are working.

Potential improvements:

- More convincing crater rim geometry.
- Better lunar regolith material.
- Subtle surface roughness variation.
- Small secondary craters.
- Scattered rocks.
- More interesting transitions between crater interiors and plains.
- Improved Earth atmosphere/cloud detail if appropriate.
- Better integration of geometric structures into crater terrain.

Consider subtle regolith accumulation around the structures.

Do not clutter the lunar surface or compromise its clean graphic quality.

The existing composition is one of the strengths of the scene.

## 6.3 Mars — substantial environment redesign

This environment needs the most substantial geological improvement.

The current landscape looks too flat and uniform, and the pyramids feel somewhat disconnected from their surroundings.

The objective is to create a distinctive Martian landscape with a stronger sense of scale, geology, and atmospheric depth.

### Geological formations

Introduce a more interesting landscape using:

- Eroded mesas.
- Distant rocky ridges.
- Exposed bedrock.
- Sedimentary formations.
- Erosion channels.
- Large rock outcrops.
- Occasional unusual geological landmarks.
- Subtle terrain elevation changes.

Use a combination of existing terrain refinement and additional authored formations.

Avoid filling the landscape with arbitrary objects.

### Pyramid integration

Improve the relationship between the pyramids and surrounding terrain.

Consider:

- Partial sand burial around their bases.
- Weathered stone.
- Surface damage.
- Broken or fractured sections.
- Accumulated debris.
- Natural erosion.
- More convincing ground contact.
- Better shadow interaction.

Give the pyramids subtle differences in age and condition.

Preserve their recognizable silhouettes.

Do not turn them into elaborate fantasy buildings.

### Martian materials

Develop a more sophisticated material system combining:

- Fine red dust.
- Exposed darker rock.
- Rocky patches.
- Wind-shaped sediment.
- Surface variation.
- Subtle mineral differences.

Avoid a single uniform beige/orange ground material.

### Martian atmosphere

This should have a distinct identity.

Investigate:

- Dusty atmospheric scattering.
- Distance-dependent contrast.
- More nuanced red/orange coloration.
- Subtle atmospheric haze.
- More convincing sky gradients.
- Better separation between foreground and distant pyramids.

Distant structures should naturally integrate into the atmosphere rather than appearing like scaled-down foreground objects.

The environment should feel vast and ancient.

## 6.4 Underwater — substantial environmental redesign

The current environment has an interesting foundation but lacks convincing underwater depth and geological identity.

Do not simply add coral or generic underwater plants.

We want an unusual alien underwater landscape.

### Seabed

Improve the seabed with:

- Sand ripples.
- Sediment deposits.
- Rocky outcrops.
- Submerged geological formations.
- Mineral formations.
- Coral-like structures where appropriate.
- Geological fractures.
- More varied terrain elevation.

Develop an interesting distribution of geological features.

### Water column

This is a major priority.

The environment currently looks as though the terrain is sitting in front of a blue background.

Make the water feel like an actual physical medium.

Investigate:

- Volumetric scattering.
- Distance-dependent light absorption.
- Suspended particles.
- Subtle drifting sediment.
- Depth-dependent visibility.
- Light shafts.
- Atmospheric color variation.
- Subtle movement in the water column.

Avoid excessive particle density.

The goal is depth and atmosphere, not visual clutter.

### Central structure

Improve its integration into the environment.

Consider:

- Sediment around foundations.
- Submerged terrain partially obscuring its base.
- Geological formations around the structure.
- External pipes and structural elements.
- Localized lighting.
- Mineral or organic buildup where appropriate.

The structure should look as though it belongs underwater rather than having been placed on top of an existing terrain mesh.

### Underwater lighting

Develop a more sophisticated lighting identity.

Experiment with:

- Deep blue/indigo ambient illumination.
- Localized illumination around the structure.
- Subtle red or warm accents.
- Volumetric light shafts.
- Distance-dependent color absorption.
- Better separation between foreground and background.

The result should feel mysterious, deep, and distinctly underwater.

## 6.5 Canyon (dusk/night) — dedicated hero environment

This deserves special consideration.

The wide canyon and the ground-level character encounter have different compositional requirements.

Do not assume that the existing canyon terrain will automatically look good when viewed from ground level.

Create a deliberately designed hero environment around the campfire.

### Hero clearing

Design the immediate campfire area as a distinct location.

Potential elements:

- A natural clearing in the canyon.
- An interesting sandstone formation behind the characters.
- Several large boulders.
- Fractured rock deposits.
- Smaller geological debris.
- Subtle terrain undulation.
- Distinctive cliff formations.
- Sparse vegetation where appropriate.

The area must remain relatively open around the characters.

Do not clutter the immediate interaction space.

### Canyon walls

Pay particular attention to the appearance of nearby cliff faces.

The existing wide-angle views reveal some surfaces that appear stretched, repetitive, or insufficiently detailed at close range.

Inspect the actual geometry and UVs.

Investigate:

- More convincing sandstone strata.
- Additional geometric variation.
- Layered rock geometry.
- Erosion channels.
- Fractured ledges.
- Natural overhangs.
- Surface displacement.
- Normal maps.
- Roughness variation.

Where a single terrain mesh cannot provide the necessary close-up quality, consider additional authored rock formations or layered geometry.

Do not simply apply more high-frequency texture noise to a fundamentally unconvincing surface.

### Ground-level composition

Create better foreground, midground, and background relationships.

The campfire should occupy a deliberate clearing, with interesting geological features framing the location.

Maintain enough open space for the alien, sasquatches, and UFO.

Ensure the background supports the character encounter rather than competing with it.

### Dusk/night lighting

Establish a coherent environmental lighting setup.

Consider:

- Warm localized campfire illumination.
- Cooler ambient canyon lighting.
- Subtle reflected light on rock formations.
- Atmospheric depth.
- Appropriate shadow distribution.
- Better silhouette separation.

The environment should remain visually interesting without being excessively illuminated.

The nighttime canyon should have its own visual identity, not simply be the daytime canyon with reduced brightness.

---

# 7. Dedicated Cinematic Environment Detail

In addition to general geological refinement, identify a handful of distinctive environmental features that make each location memorable.

We want authored environmental landmarks, not thousands of arbitrary props.

Potential directions:

**Canyon**
- Distinctive eroded arches.
- Unusual fractured formations.
- Large layered rock outcrops.
- Interesting geological silhouettes.

**Moon**
- Additional crater formations.
- Partially buried structures.
- Unusual geological features surrounding the existing domes.

**Mars**
- Massive eroded mesas.
- Distinctive rock monoliths.
- Partially buried architectural remnants.
- Distant geological formations.

**Underwater**
- Unusual mineral formations.
- Alien geological structures.
- Submerged rock formations.
- Occasional bioluminescent elements.

These are creative directions, not mandatory asset lists.

Only introduce features that improve the composition and sense of place.

Avoid generic fantasy scenery, arbitrary sci-fi props, excessive vegetation, and repetitive procedural rock formations.

---

# 8. Cinematic Lighting and Post-Processing

Once the environments have appropriate geometry and materials, refine their cinematic presentation.

Do not rely on post-processing to conceal poor geometry or materials.

## 8.1 Lighting

Investigate:

- Better directional lighting.
- Improved shadow quality.
- Contact shadows.
- More convincing indirect illumination.
- Atmospheric scattering.
- Volumetric lighting.
- Improved emissive integration.
- Better foreground/background separation.
- More intentional color contrast.

Every environment should have a distinct lighting design.

## 8.2 Atmospheric perspective

This is especially important for:

- Canyon.
- Mars.
- Underwater.

Use distance-dependent changes in color, contrast, and visibility to establish depth.

Avoid applying generic fog uniformly across the scene.

## 8.3 Post-processing

Investigate the existing AV Gen post-processing pipeline.

Consider:

- More sophisticated tone mapping.
- Improved highlight rolloff.
- Restrained bloom.
- Subtle film grain.
- Color grading.
- Local contrast.
- Atmospheric depth.
- Selective depth of field.
- Subtle vignette.
- Appropriate emissive treatment.

Do not introduce excessive chromatic aberration, blur, bloom, or film effects.

Preserve the distinctive color identity of each environment.

The goal is to reduce the overly clean digital appearance without making everything look artificially processed.

## 8.4 Cohesive visual treatment

Develop a consistent overall rendering quality across the environments.

This does not mean identical color grading.

The canyon should remain warm and geological.

The moon should remain stark and graphic.

Mars should feel dusty and atmospheric.

Underwater should have distinctive depth and light absorption.

The commonality should be material sophistication, lighting quality, and artistic restraint.

---

# 9. Camera-Distance Quality Standards

Every environment must be evaluated at three different distances.

This is a mandatory artistic requirement.

## Establishing shot

Evaluate the environment as a large landscape.

Questions:

- Does the overall composition feel compelling?
- Are the silhouettes interesting?
- Is the terrain visually varied?
- Are there recognizable environmental landmarks?
- Does the atmosphere establish scale?
- Is the visual hierarchy clear?

## Traversal shot

Evaluate the environment from a camera moving through it.

Questions:

- Does the terrain maintain visual interest?
- Are there obvious repetitive patterns?
- Do environmental elements appear randomly distributed?
- Does the world feel spatially coherent?
- Are there interesting foreground and midground elements?
- Does the environment maintain depth while moving?

## Hero shot

Evaluate the environment from a close camera position.

Questions:

- Do textures hold up?
- Does geometry look convincing?
- Are materials sufficiently sophisticated?
- Are there obvious UV stretching artifacts?
- Do geological formations have believable structure?
- Does the lighting reveal the surface detail?
- Does the environment still feel authored?

For the canyon character encounter, the hero-shot evaluation is particularly important.

Do not accept an environment simply because it looks good from a distance.

---

# 10. Blender Workflow

Use Blender as a substantial environment-art tool.

Inspect the existing meshes and materials before deciding what needs improvement.

Potential workflows:

- Import existing environment assets.
- Inspect topology and UVs.
- Sculpt additional geological detail.
- Create layered rock geometry.
- Author high-resolution materials.
- Generate normal and roughness maps.
- Bake procedural detail.
- Create displacement.
- Create custom geological formations.
- Improve ground materials.
- Prepare additional environment assets.
- Export and validate in AV Gen.

Use headless Blender or existing Blender MCP tooling as appropriate.

Prefer free, high-quality textures and assets where useful.

However, don't turn this into an asset-acquisition project.

Our existing environment geometry is the foundation. Improve it wherever possible.

Preserve original assets and maintain organized source files.

All modifications must be validated in the actual AV Gen renderer.

---

# 11. Implementation Strategy and Prioritization

Do not modify all six environments simultaneously.

Work in the following order.

## Phase A — Establish baseline and artistic direction

1. Inspect the existing scene.
2. Audit all environment meshes and materials.
3. Inspect the existing Cinematic Critic.
4. Establish baseline evaluations.
5. Identify the most important visual weaknesses.
6. Document the environment-art direction.
7. Produce a detailed implementation plan.

## Phase B — Establish the environment quality benchmark

Polish Canyon (day).

Use this as the benchmark for:

- Geological detail.
- Material sophistication.
- Terrain variation.
- Lighting.
- Atmospheric depth.

Do not redesign it.

Once its improvements are validated through the Cinematic Critic, establish it as the visual quality reference for the remaining environments.

## Phase C — Improve the ground-level canyon encounter

Create the dedicated campfire clearing.

Improve nearby canyon walls, terrain, geological formations, and lighting.

Evaluate at both establishing and close-up camera positions.

The encounter should feel like a deliberately designed location.

## Phase D — Rework Mars

Focus on:

- Geological formations.
- Terrain variation.
- Pyramid integration.
- Material quality.
- Atmospheric depth.
- Composition.

Use the Cinematic Critic to identify remaining weaknesses.

## Phase E — Rework Underwater

Focus on:

- Seabed geometry.
- Geological formations.
- Sediment.
- Water column.
- Volumetric scattering.
- Lighting.
- Structure integration.

## Phase F — Moon polish

Make only improvements that meaningfully enhance the existing scene.

## Phase G — Common material and cinematic refinement

Review all six environments.

Identify opportunities for shared material improvements, consistent lighting quality, and appropriate post-processing.

Do not force uniformity where the artistic direction calls for differences.

---

# 12. Agent Workflow and Engineering Requirements

Before implementation:

- Inspect the relevant code, scene definitions, assets, and documentation.
- Use deep research where useful.
- Use up to two parallel subagents for independent audit/research work.
- Have agents produce findings before allowing overlapping implementation.
- Create a detailed plan before modifying code or assets.

Suggested parallel work:

**Agent A — Environment and asset audit**
- Inspect all six environments.
- Audit geometry, materials, UVs, and textures.
- Identify major visual problems.
- Research appropriate free resources where useful.
- Produce environment-specific recommendations.

**Agent B — Renderer and cinematic evaluation audit**
- Inspect the existing Cinematic Critic.
- Determine the correct invocation workflow.
- Audit material and post-processing capabilities.
- Identify renderer limitations affecting environment quality.
- Produce targeted technical recommendations.

Keep implementation coordinated.

Use a clean branch or worktree.

Do not allow multiple agents to modify the same environment assets simultaneously.

Do not introduce unnecessary renderer architecture changes.

Keep reusable improvements in shared systems where appropriate.

Preserve all existing scene transitions, camera paths, portals, animations, and audio-reactive functionality.

This is an environment-art initiative, not a scene redesign or audio-system rewrite.

---

# 13. Final Validation and Deliverables

The task is not complete until the actual AV Gen renders demonstrate meaningful improvement.

Run the existing Cinematic Critic repeatedly throughout the work.

At completion, provide:

1. Initial environment audit.
2. Environment-art direction document.
3. Baseline cinematic evaluation results.
4. Description of the improvements made to each environment.
5. Material and shader improvements.
6. Geometry and terrain improvements.
7. Lighting and atmospheric improvements.
8. Any targeted renderer changes.
9. Updated Blender source projects.
10. Organized exported assets.
11. Before-and-after renders from identical camera positions.
12. Final Cinematic Critic evaluation results.
13. Remaining visual shortcomings and recommended next steps.

Include a comparison of the original and final evaluations.

Identify which changes produced the most meaningful visual improvements.

Do not claim a visual improvement simply because more assets or code were added.

The final assessment must be based on actual rendered results.

---

# 14. Artistic Quality Bar

The finished environments should feel like places that have been deliberately designed by an experienced environment artist.

Avoid:

- Generic procedural terrain.
- Uniformly distributed rocks.
- Repetitive textures.
- Excessive surface noise.
- Unnecessary geometry.
- Overly photorealistic asset mismatches.
- Generic AI-generated scenery.
- Excessive vegetation.
- Flat lighting.
- Uniform fog.
- Excessive bloom.
- Overly clean synthetic surfaces.
- Random sci-fi props.
- Unnecessary architectural additions.

Prioritize:

- Strong environmental silhouettes.
- Interesting geological formations.
- Controlled detail distribution.
- Sophisticated material response.
- Believable terrain transitions.
- Natural erosion and weathering.
- Atmospheric depth.
- Intentional lighting.
- Memorable environmental landmarks.
- Strong foreground/midground/background relationships.

**The central artistic principle: more detail is not automatically better. Better composition, better material variation, better lighting, and more intentional placement of environmental features are what make an environment feel professionally finished.**

We want Strange Attractor to feel like a collection of distinct, memorable cinematic worlds, all belonging to the same artistic universe.

Preserve what is already working. Improve what is not. Use the Cinematic Critic as an iterative artistic feedback mechanism throughout the entire process.

The final result should be visibly more polished, more convincing, and more carefully art-directed without losing the distinctive visual identity that makes Strange Attractor interesting.
