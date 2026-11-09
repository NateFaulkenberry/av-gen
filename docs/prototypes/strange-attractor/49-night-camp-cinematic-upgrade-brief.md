# Strange Attractor: Cinematic Character, Creature, Lighting & Hero Asset Upgrade (the owner's brief)

Received 2026-11-03. The owner's brief is reproduced below, verbatim. Reference screenshot (read-only,
never commit): `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3e/00-owner-refs/night-camp-cinematic-upgrade-reference.png`.

## Coordinator split (how this brief is executed)

The owner caps concurrent agents at two, and only one agent may modify the scene or shared assets. So
the brief's "Subagent A / Subagent B" becomes two existing lanes:

- **Hero-asset agent** (new; worktree `../av-gen-sa-hero`, branch `scene/sa-hero`). Owns Phases 1.1, 1.2,
  2 (alien and Sasquatch models, materials, fur, animation and the alien's gesturing), 3.1–3.2 (the
  campfire asset: logs, flames, embers, smoke elements), 4.1–4.2 (the UFO model, materials and emissives)
  and 6.
  - Works in Blender on copies; originals are never overwritten.
  - Exports finished GLBs and textures to `assets/strange-attractor/hero/`, gitignored and shared with
    the art agent's worktree.
  - Commits only its Blender scripts, docs and asset records. It may render test shots in its own
    worktree through gpu-lock, but it does NOT edit the Strange Attractor scene files, the builders'
    scene output or the renderer.
  - Material or renderer gaps it finds go to the coordinator, who routes them to the art agent.
- **Strange Attractor art agent** (worktree `../av-gen-attractor`, branch `scene/strange-attractor`).
  Owns Phases 1.3, 3.3, 4.3, 5, 7 and 8:
  - the renderer and material audit;
  - fire, UFO and character lighting, atmosphere and the ground;
  - renderer improvements: this already includes the shadow-budget and bias ADR and the other
    48-night-lighting-research.md items;
  - integrating the hero assets into the scene;
  - baseline and incremental renders from the reference camera.

  This brief extends and supersedes M3e §8–§10 of 47- for the night camp.

Hand-offs:
- The hero-asset agent hands each finished asset (alien, Sasquatches, campfire, UFO) to the coordinator
  with a Blender preview and an AV Gen test render.
- The art agent integrates it, lights it, and renders the reference-camera comparison.
- One research agent only (owner rule). No helper subagents.

---

# AV Gen — Strange Attractor: Cinematic Character, Creature, Lighting & Hero Asset Upgrade

## Objective

Perform a comprehensive visual-quality upgrade of the Strange Attractor nightime canyon scene, focusing on the characters, creatures, campfire, UFO, materials, and environmental lighting.

The current scene has a strong underlying concept and composition, but several assets look noticeably like low-quality game-engine models. We want to move toward a convincing cinematic VFX aesthetic.

Blender is installed and available. You may use Blender headless, Python scripting, or the existing Blender MCP tooling if useful.

The screenshot provided with this task is a reference for the current scene and its visual shortcomings. Pay particular attention to the alien, sasquatches, campfire, and UFO.

The primary objective is to improve the existing assets wherever possible rather than automatically replacing them.

---

# PHASE 1 — Comprehensive Asset and Renderer Audit

Before making changes, inspect the existing Strange Attractor implementation and all associated assets.

### 1.1 Character and creature audit

Identify the actual models currently used for:

- The alien.
- All three sasquatches.

For each model, investigate:

- Polygon and vertex counts.
- Mesh topology and geometric detail.
- Anatomical proportions and silhouette.
- Existing UV mapping.
- Texture resolution and quality.
- Existing material assignments.
- Normal map availability.
- Roughness and metallic map availability.
- Texture coordinate quality.
- Rigging and skeletal structure.
- Existing idle animations.
- Skinning quality and deformation artifacts.
- Whether the model has separate meshes for eyes, teeth, claws, hair, fur, etc.
- Whether Blender can import and meaningfully improve the model.

Determine whether the existing models are fundamentally adequate or whether their geometry is too primitive to achieve convincing cinematic results.

**Important:** We already have idle animations. Preserve and improve the existing animation setup rather than unnecessarily replacing it. Investigate whether the existing rigs and animations are good enough to retain.

### 1.2 Free model research

Perform actual research into potentially better freely available alternatives.

Look beyond the assets we currently have, but don't assume a replacement will necessarily be better.

Potential sources include:

- Sketchfab (downloadable models with appropriate licenses).
- Fab.
- Blend Swap.
- Poly Haven.
- Other reputable free 3D asset repositories.
- Free photogrammetry and scanned-asset collections.

Research suitable alternatives for:

1. Realistic humanoid alien.
2. Realistic Bigfoot / Sasquatch creatures.
3. High-quality UFO / flying saucer.

Evaluate the available models based on:

- Actual geometric quality.
- Anatomical realism.
- Texture quality.
- UV mapping.
- Rigging and animation availability.
- Material quality.
- Licensing and redistribution permissions.
- Compatibility with our Blender and AV Gen asset pipeline.

Do not recommend models based solely on attractive thumbnails. Inspect downloadable files, screenshots, topology previews, texture maps, or other available evidence.

Where possible, download and inspect promising candidates.

Create a comparison between the existing models and the best free alternatives.

For each asset, make an explicit recommendation:

- Retain and enhance existing model.
- Retain existing geometry but substantially upgrade materials and textures.
- Modify geometry in Blender.
- Replace with a better freely available model.

If no substantially better free alternative exists, retain the existing asset and focus on maximizing its quality.

Do not waste time searching indefinitely for an ideal model that doesn't exist in the free asset ecosystem.

### 1.3 Renderer and material pipeline audit

Inspect the actual AV Gen renderer and determine what material features are supported.

Investigate:

- PBR material support.
- Albedo/base color textures.
- Normal maps.
- Roughness maps.
- Metallic maps.
- Ambient occlusion maps.
- Emissive textures.
- Transparency and alpha handling.
- Texture resolution and filtering.
- Tangent-space normal mapping.
- Subsurface scattering support or possible approximations.
- Shadow casting and receiving.
- Emissive lighting contributions.
- Volumetric lighting.
- Environment lighting and indirect illumination.

Determine which improvements can be made entirely in Blender and which require modifications to the AV Gen renderer.

Do not assume a Blender material will transfer correctly into AV Gen. Validate the actual exported assets in our renderer.

Document any important material or rendering limitations before implementation.

---

# PHASE 2 — Character and Creature Quality Upgrade

The characters are currently the weakest visual elements in the scene.

We want them to look like professionally authored cinematic creatures rather than generic game assets.

## 2.1 Alien

The existing alien is usable but let's investigate some

Potential improvements:

- High-quality skin textures.
- Detailed normal maps.
- Roughness variation.
- Subtle skin imperfections.
- Anatomical surface detail.
- More convincing facial features.
- Better eye materials.
- Realistic eye reflections and highlights.
- More sophisticated shading.
- Improved proportions if the existing geometry allows it.
- More convincing hands, feet, joints, and facial anatomy.

Consider whether the alien would benefit from:

- Blender sculpting.
- Multiresolution subdivision.
- Displacement mapping.
- Additional geometric detail.
- Separate eye geometry.
- Procedurally generated skin detail.
- High-resolution baked normal maps.

The target is an interesting, slightly uncanny extraterrestrial humanoid.

Avoid making it look like a cartoon alien, generic plastic mannequin, or overly smooth toy.

Do not unnecessarily redesign its silhouette if the existing model can be improved.

### Alien animation

Retain my request to make the alien appear as though it is talking to the squatches in a neutral stance

Avoid exaggerated procedural movement.

The alien should feel like a living creature standing in the environment.

## 2.2 Sasquatches

The three sasquatches currently look more like dark, irregular rocks than recognizable creatures.

This is a major visual problem.

We need to make them immediately recognizable as large, organic, humanoid creatures.

Priorities:

**Anatomy and silhouette**
- Clearly identifiable head and shoulders.
- Convincing neck and torso.
- Recognizable arms and legs.
- Appropriate muscular proportions.
- More convincing hands and feet.
- Better facial silhouettes.

**Fur and surface quality**
- Detailed fur textures.
- Layered roughness.
- Normal and bump detail.
- Color variation across the body.
- Appropriate fur direction.
- More convincing facial and body transitions.
- Individualized color and material variation between the three creatures.

Investigate several approaches to fur:

1. High-quality fur texture and normal maps.
2. Fur cards.
3. Geometry-based fur.
4. Blender particle-based hair or curves.
5. A hybrid approach using actual fur around the head and shoulders with textured fur elsewhere.

Since this is an offline scene, do not artificially constrain the solution to inexpensive real-time fur techniques.

However, choose an approach that can actually be exported and rendered successfully by AV Gen.

If necessary, use Blender to bake detailed fur appearance into textures or geometry.

**Important:** The creatures must remain readable under nighttime lighting. Do not bury their anatomy in darkness.

### Sasquatch animation

We already have idle animations.

Preserve them and assess their quality.

Improve presentation where appropriate:

- Subtle breathing.
- Natural head movement.
- Slight posture adjustments.
- Better weight distribution.
- Small independent movements between the three creatures.

Avoid synchronized robotic movement.

The three sasquatches should feel like individual creatures sharing a campfire.

---

# PHASE 3 — Campfire Reconstruction and Lighting

The current campfire is visually unconvincing. It looks like a bright particle effect sitting on top of a dark object.

This should become a convincing cinematic campfire.

## 3.1 Fire geometry and appearance

Improve the actual fire rather than simply increasing particle counts or brightness.

Investigate:

- Layered flame shapes.
- Multiple flame structures with different heights.
- Natural flame turbulence.
- Color variation from dark orange through yellow and white-hot regions.
- Ember generation.
- Occasional sparks.
- Burning logs.
- Charred wood.
- Glowing embers around the base.
- Subtle smoke.

The logs should be recognizable as actual pieces of wood arranged in a believable campfire formation.

Include convincing material detail:

- Bark.
- Cracked surfaces.
- Charred sections.
- Glowing cracks.
- Ash.
- Ember accumulation.

The fire should have a recognizable physical structure and not resemble a generic glowing particle fountain.

## 3.2 Fire simulation and volumetrics

Investigate whether we can use:

- Blender-created fire and smoke assets.
- Animated volumetric textures.
- Procedural volumetric shaders.
- Particle-based embers.
- Layered transparent flame geometry.
- Pre-rendered or baked volumetric elements.

Choose the approach that produces the best result in AV Gen.

Since the scene is offline-oriented, more expensive techniques are acceptable.

## 3.3 Fire illumination

This is particularly important.

The fire must visibly illuminate its surroundings.

Implement or improve:

- Dynamic orange/yellow illumination.
- Ground illumination around the fire.
- Light attenuation with distance.
- Flickering intensity.
- Subtle color variation.
- Shadows cast by nearby objects.
- Light interaction with the sasquatches.
- Light interaction with the alien.
- Reflected light on nearby rocks.

The fire should illuminate the undersides and nearby surfaces of the creatures.

It should also create an atmospheric pool of warm illumination against the much cooler nighttime environment.

Avoid an unnaturally uniform orange lighting effect.

---

# PHASE 4 — UFO Material and Appearance Upgrade

The UFO currently looks like a relatively simple, smooth 3D toy.

Its overall shape can remain, but its construction and materials need substantially more sophistication.

## 4.1 Surface materials

Investigate the existing geometry and material assignments.

Upgrade the UFO with:

- Detailed metallic surface textures.
- Roughness variation.
- Subtle brushed-metal appearance.
- Panel divisions.
- Mechanical seams.
- Small surface imperfections.
- Appropriate metallic reflections.
- Darker recessed sections.
- Detailed underside geometry.
- More convincing glass or cockpit materials.

The UFO should feel like a manufactured physical object rather than a perfectly smooth primitive.

Use Blender to improve the geometry if necessary.

Add detail selectively. Avoid covering the entire vehicle in arbitrary sci-fi greebles.

The aesthetic should be sophisticated, restrained, and believable.

## 4.2 Emissive elements

Improve the existing lights.

Potential additions:

- Small navigation lights.
- Recessed illuminated sections.
- Subtle underbody illumination.
- Individual light segments around the perimeter.
- Cockpit illumination.
- Subtle internal lighting.

Avoid excessive neon or rainbow-colored lights.

## 4.3 UFO illumination

The UFO currently produces a large pool of light beneath it.

Improve this effect considerably.

Investigate:

- A convincing downward-facing light source.
- Volumetric scattering.
- Atmospheric interaction.
- Illumination on nearby rocks.
- Illumination on the ground.
- Soft light falloff.
- Subtle atmospheric haze around the craft.

The UFO should feel as though it is genuinely hovering above the terrain.

The light should integrate the craft into the environment rather than appearing as an artificial spotlight placed underneath it.

---

# PHASE 5 — Overall Cinematic Lighting and Environment Integration

This is a major priority.

Even excellent models will look like CG assets if their lighting doesn't integrate with the environment.

The current screenshot has several issues:

- Characters are poorly separated from the background.
- The sasquatches lose their silhouettes.
- The alien looks disconnected from the ground.
- The fire produces an overly localized lighting effect.
- The UFO lighting is visually detached from its surroundings.
- The overall image lacks convincing atmospheric depth.

We need to approach this as a cinematic lighting exercise.

## 5.1 Establish a lighting hierarchy

Develop a coherent lighting setup with three primary sources:

1. Campfire: warm orange/yellow illumination.
2. UFO: cool white/blue illumination.
3. Nighttime environment: subtle ambient illumination.

These should interact naturally.

Preserve the contrast between the warm campfire and the cool UFO.

Do not flatten the scene with excessive ambient illumination.

## 5.2 Character lighting

Give particular attention to the sasquatches and alien.

Implement:

- Subtle rim lighting.
- Better silhouette separation.
- Contact shadows.
- Ground contact.
- Appropriate shadow softness.
- Warm firelight on nearby surfaces.
- Cool UFO light where appropriate.
- Better facial visibility.

The sasquatches should have enough lighting to reveal their fur and anatomy without looking artificially illuminated.

The alien should receive appropriate reflected light from both the campfire and the environment.

## 5.3 Atmospheric depth

Investigate:

- Volumetric haze.
- Light scattering.
- Subtle smoke.
- Atmospheric perspective.
- Distance-dependent contrast.
- Ground-level atmospheric effects.
- Light interaction with smoke and dust.

The scene should feel like a real nighttime location rather than a collection of models sitting on a flat rendered surface.

Be restrained. We want cinematic atmosphere, not excessive fog.

## 5.4 Ground and surrounding environment

The ground currently has a relatively flat, repetitive appearance.

Improve it with:

- More convincing surface texture.
- Roughness variation.
- Normal detail.
- Small rocks and debris.
- Subtle terrain variation.
- Appropriate light response.
- Better integration of contact shadows.

The immediate area around the fire should contain some believable environmental detail.

Do not unnecessarily redesign the entire canyon.

---

# PHASE 6 — Blender Asset Authoring and Integration

Use Blender extensively where it provides a meaningful improvement.

Potential workflows:

- Import existing character models.
- Inspect and improve UVs.
- Create or improve material assignments.
- Author high-resolution textures.
- Generate normal and roughness maps.
- Sculpt additional detail.
- Create fur.
- Improve geometry.
- Bake appropriate maps.
- Prepare optimized exports.
- Validate animations.
- Export and test in AV Gen.

Use free resources where appropriate.

Do not assume that downloading a higher-quality model automatically makes it usable. Check topology, materials, rigging, file size, licensing, and actual compatibility.

Maintain the original source assets.

Never destructively overwrite the only copy of an existing model.

Keep Blender project files and intermediate assets organized.

Ensure that any external assets have their license and source information documented.

---

# PHASE 7 — AV Gen Renderer Improvements

If the audit identifies renderer limitations that prevent the assets from looking convincing, implement targeted improvements.

Potential areas:

- Better normal-map handling.
- Better roughness response.
- Better metallic shading.
- Better emissive materials.
- More accurate shadowing.
- Improved contact shadows.
- Subsurface approximation.
- Better volumetric integration.
- Improved texture quality.
- Better indirect illumination.

Do not introduce arbitrary rendering changes without demonstrating their necessity.

Do not rewrite the renderer.

The goal is to enable the cinematic quality required by this particular scene, while keeping changes reusable by other AV Gen scenes.

Because this is an offline-oriented scene, prioritize image quality over frame time.

If a feature is too expensive for the live renderer but valuable for offline rendering, investigate a dedicated offline-quality path rather than compromising the visual result.

---

# PHASE 8 — Visual Validation and Iteration

This phase is essential.

Do not consider the task complete merely because the models import successfully or the materials compile.

We need to see actual rendered improvements.

## 8.1 Establish a baseline

Capture the current scene from the same camera position as the supplied reference screenshot.

Preserve the original render for comparison.

## 8.2 Incremental visual testing

Render the scene after each major upgrade:

1. Alien.
2. Sasquatches.
3. Campfire.
4. UFO.
5. Character lighting.
6. Environmental lighting.
7. Final combined scene.

This makes it possible to identify which changes are actually improving the image.

## 8.3 Final comparison

Produce before-and-after screenshots using the same camera position and approximately the same framing.

Evaluate:

- Character realism.
- Creature anatomy.
- Surface detail.
- Material quality.
- Lighting integration.
- Shadow quality.
- Atmospheric depth.
- Overall cinematic appearance.

Inspect the renders yourself and make additional adjustments where the result still looks artificial.

Do not stop after the first successful implementation.

---

# Execution Instructions

## Research and planning

Before implementation:

1. Inspect the current Strange Attractor scene.
2. Identify all relevant assets and source files.
3. Audit the renderer's actual material capabilities.
4. Research better free models and textures.
5. Inspect the existing models in Blender.
6. Establish which assets should be retained, modified, or replaced.
7. Produce a detailed implementation plan.

You may use up to two parallel subagents for independent work, such as:

- Subagent A: Character/creature asset research, Blender inspection, and free model alternatives.
- Subagent B: Renderer/material/lighting audit and potential technical improvements.

Keep the work coordinated. Do not have multiple agents independently modify the same scene or shared assets.

## Implementation

After the plan:

- Work in a clean branch or worktree.
- Preserve existing scene functionality.
- Retain existing idle animations wherever possible.
- Keep original source assets intact.
- Use Blender for substantial asset improvements.
- Integrate assets into the actual AV Gen pipeline.
- Test the actual rendered scene, not just Blender previews.
- Avoid unnecessary architectural changes.

## Documentation

Document:

- Existing assets and their quality assessment.
- Alternative models investigated.
- Sources and licenses for downloaded assets.
- Blender modifications.
- New textures and materials.
- Renderer changes.
- Export settings.
- Any remaining visual limitations.

Update relevant AV Gen documentation and scene notes.

## Final deliverables

At completion, provide:

1. Asset audit and retain/replace recommendations.
2. Comparison of existing assets against researched alternatives.
3. Updated alien model and materials.
4. Updated sasquatch models and materials.
5. Upgraded campfire.
6. Upgraded UFO.
7. Improved cinematic lighting.
8. Any necessary renderer improvements.
9. Organized Blender source projects and exported assets.
10. Before-and-after screenshots.
11. Performance/rendering observations for the offline scene.
12. A summary of remaining limitations and recommended next steps.

---

# Artistic Direction and Quality Bar

The target is cinematic VFX quality, not generic game-engine graphics.

Think professionally produced science-fiction film, practical creature effects, detailed creature sculpts, realistic materials, sophisticated nighttime cinematography, and carefully controlled atmospheric lighting.

Avoid:

- Plastic-looking characters.
- Smooth, featureless geometry.
- Cartoon-like anatomy.
- Generic AI-generated realism.
- Excessive sci-fi greebles.
- Overly bright neon materials.
- Flat lighting.
- Excessive fog.
- Unconvincing particle effects.
- Unnecessary scene redesign.

Preserve the existing Strange Attractor artistic direction and composition.

**The most important requirement: prioritize the actual rendered image.** A technically impressive model or elaborate Blender project is not a success unless it produces a visibly better result in AV Gen.

We want the final campfire encounter to look like a believable cinematic moment: three recognizable sasquatches gathered around a real fire, an unusual alien standing nearby, and a detailed flying saucer hovering over the canyon — all convincingly integrated into the same environment.
