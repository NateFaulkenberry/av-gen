# Bioluminescent Ecosystem: Professional VFX Art Pass

(The owner's spec, verbatim, 2026-10-08, given in reference to `~/Desktop/av-gen-review/45-bioluminescent-valley/cp4/sbs-all.png` (CP4, fb29efb0). It supersedes the coordinator's CP4b note where they conflict. Coordinator's notes:
- "The visual critic" is the Creative Critic service in `~/Documents/GitHub/creative-critic`. If it is down, restart it with `~/Documents/GitHub/creative-critic/.venv/bin/critic start --daemon`.
- Blender MCP needs Blender running with the MCP add-on connected. Check that first.
- Blender-authored assets follow the repo rules: no committed file over 1 MB. Commit the Blender Python or generator script and keep the exported GLBs and textures generated and gitignored, the same way the valley's other generated meshes are handled.)

# AV Gen — Bioluminescent Ecosystem: Professional VFX Art Pass

## Mission

Take the existing bioluminescent ecosystem scene from its current procedural-CG appearance to a genuinely beautiful, professionally art-directed audiovisual environment.

The attached `sbs-all.png` is the primary visual reference. The left side contains the target artwork; the right side contains the latest AV Gen renders. Retrieve the latest version from the repository before beginning, because the image may have changed since previous iterations.

The latest art pass has improved the valley's overall atmosphere and established a more useful meadow composition. Preserve those gains. The main remaining problems are the synthetic-looking botanical forms, uniform glowing details, flat water, weak material integration, and lack of convincing visual richness at close range.

Do not start over. Do not blindly implement the previous prompt. Inspect the newest renders and current implementation, then choose the smallest set of changes that will produce the largest visible improvement.

**The objective is image quality, not procedural complexity.**

You have permission to use Blender through MCP for modeling, materials, geometry nodes, asset creation, scattering, look development, and reference rendering. You may also use a hybrid 2.5D approach if it produces a more convincing result. AV Gen must remain the primary live audiovisual instrument.

The final scene must work within AV Gen's existing live and offline rendering architecture, or use a clearly justified extension of it.

---

## Phase 1 — Diagnose the current renders

Retrieve the latest comparison image and render the current scene through the existing visual critic.

Inspect the actual source code, assets, materials, camera, scene configuration, lighting, post-processing, and renderer capabilities. Determine what is really producing each visible element.

For each of the four compositions, record:

* What improved in the latest pass.
* What remains visibly different from the target.
* The three most important remaining image-quality problems.
* The likely underlying cause of each problem.
* The smallest practical experiment that could verify the diagnosis.

Do not confuse an object being present with that object looking convincing.

A scene can contain mountains, fungi, vegetation, a river, fog, and particles and still look artificial. Evaluate the rendered image's composition, silhouettes, material quality, light relationships, depth, and visual rhythm.

Do not spend days researching or redesigning the architecture before rendering anything. Produce a concise diagnosis, then begin the first visual experiment immediately.

## Phase 2 — Build exceptional organic hero assets

**This is the highest-priority task. Start here.**

The macro comparison is the clearest evidence of the current quality ceiling. The existing render relies heavily on glowing circles, rings, thin stems, and repeated dotted surfaces. The reference instead features distinctive botanical structures, delicate filaments, layered organic forms, translucent materials, and detailed silhouettes.

The current procedural vocabulary is too primitive to deliver the desired image, regardless of how many particles or instances are added.

### Use Blender if it helps

Inspect the available Blender MCP tools and the existing AV Gen asset-import pipeline. Determine the fastest reliable way to create a few exceptional hero assets and bring them into AV Gen.

Do not assume Blender must be used for everything. Use it where its modeling, material, and asset-authoring capabilities provide a clear advantage over building the same forms procedurally inside AV Gen.

Create a small initial library of approximately five to eight distinct botanical archetypes:

1. A delicate translucent fungus with a layered cap, detailed rim, and visible underside structure.
2. An intricate branching organism with curved, tapered filaments.
3. A folded cup, trumpet, or bell-shaped organism with a distinctive silhouette.
4. A cluster of small luminous structures connected by an irregular organic branching body.
5. A broad, delicate frond or leaf with layered surfaces and subtle transmission.
6. A tall stalk with a complex head and smaller secondary branches.
7. A low-lying moss or fungal mat with fine surface detail.
8. One unusual hero organism designed specifically to provide a memorable focal point.

These are starting points, not a mandate to make generic mushrooms. Design a coherent family of alien life forms that feels native to this particular world.

### Asset design requirements

Each hero asset must have:

* A distinctive silhouette.
* Organic curvature and nonuniform proportions.
* Meaningful primary and secondary structure.
* Irregularity that feels intentional rather than random.
* Surface detail that follows the form.
* Localized luminous anatomy rather than uniform self-illumination.
* Material variation across its surface.
* A readable shape under restrained lighting.
* An attractive appearance both close up and at normal scene scale.

Use details such as membranes, ribs, gills, veins, pores, layered tissue, filaments, translucent edges, and branching structures where they fit the design.

Do not decorate a sphere or cylinder and call it a finished organism. Primitive geometry may be used internally, but the final silhouette and material response must feel designed.

### Materials

Treat the material as part of the asset's anatomy.

Differentiate between:

* Thin translucent membranes.
* Thick organic tissue.
* Dark stems and support structures.
* Fine filaments.
* Bright internal veins.
* Localized glowing tips.
* Rough or velvety surfaces.
* Smooth, gelatinous surfaces.

Use transmission, backlighting, normal variation, roughness variation, layered shading, and emission gradients wherever supported by the existing renderer or asset pipeline.

If the real-time renderer cannot reproduce a particular material feature, test an inexpensive approximation before proposing new renderer infrastructure.

**Do not solve weak geometry by increasing emission or bloom.** The object must have an interesting silhouette and visible structure even when its glow is restrained.

### First required render

Create a dedicated macro test scene containing:

* One hero organism.
* Several supporting organisms.
* Dark surrounding vegetation.
* A carefully placed key or backlight.
* Localized emission.
* Distinct foreground and background layers.
* Selective focus or another suitable depth-separation technique, if supported.

Render this as soon as the first hero asset is ready. Compare it with the macro reference and revise the asset before building more content.

Do not wait until the entire world is populated.

---

## Phase 3 — Run a small production-method comparison

Use the macro test to determine whether the best solution is native AV Gen geometry, Blender-assisted asset production, or a hybrid 2.5D approach.

Evaluate the approaches that are practical with the current codebase.

**Approach A: Native AV Gen**

Improve geometry, materials, distribution, lighting, and rendering using existing engine capabilities.

**Approach B: Blender-assisted assets**

Create superior organic models and materials in Blender, import them into AV Gen, and use the existing real-time scene, camera, lighting, and audio systems.

**Approach C: Hybrid 2.5D**

Use full 3D for important foreground geometry and objects requiring parallax, lighting, or reflections. Use layered cards, projected detail, baked textures, or depth-aware environmental elements where they improve visual quality without unnecessary geometry.

Render the same composition using each viable approach. Compare actual image quality, camera robustness, asset iteration speed, material integration, and performance.

Choose based on evidence. Do not migrate the entire scene to Blender or build a new rendering architecture without a demonstrated benefit.

---

## Phase 4 — Repair the riverside composition

The current riverside still reads too much like a luminous environment placed behind a broad purple water plane.

Preserve the intimate low-camera perspective and the overall scene direction. Improve the integration of the foreground, water, vegetation, and distant environment.

### Foreground and bank

* Introduce dark, irregular foreground silhouettes that frame the water.
* Use the approved hero assets rather than generic glowing primitives.
* Add believable rocks, moss, low vegetation, and organic transitions at the waterline.
* Establish a clear foreground, middle ground, and background.
* Remove or redesign unexplained pale geometric structures after identifying their source.
* Avoid large areas of empty water that contribute little to the composition.

### Water

Inspect the actual water implementation and identify why it reads as a flat purple surface.

Develop a water treatment with:

* A dark underlying color.
* Irregular banks and shallow-water transitions.
* Reflections of the actual surrounding vegetation and luminous features.
* Elongated, broken reflections rather than a uniform field of bright dots.
* Controlled roughness and subtle directional variation.
* Local ripples or deformation where useful.
* Dark regions that preserve contrast around the highlights.

Investigate the reflection capabilities already available in AV Gen. Use existing reflection techniques first. If a fully accurate solution is too expensive, test a convincing approximation appropriate to the live camera and scene.

Do not represent the river as a glowing line drawn across the terrain.

### Lighting integration

The river, rocks, plants, and atmosphere must look as though they inhabit one coherent environment.

Use selective local illumination and reflected color where feasible. The luminous vegetation should influence the surrounding image without making everything equally bright.

Produce a new riverside still and compare it with the reference before moving on.

---

## Phase 5 — Refine the valley without losing progress

The latest valley render is a useful foundation. Preserve its improved atmosphere and broad composition.

The remaining issues are the repetitive pointed mountain silhouettes, empty central regions, dotted luminous patterns, and river features that lack environmental integration.

### Terrain

Develop varied, overlapping mountain masses with different profiles, widths, scales, and degrees of detail. Avoid repeating the same triangular peak.

Use atmospheric perspective to separate distance layers. Distant terrain should have lower contrast and less detail; closer terrain should have stronger silhouettes and richer surface variation.

### River and waterfalls

Make the water system read as a continuous feature of the landscape.

Introduce a winding river, connected pools, tributaries, or occasional waterfalls where they make sense compositionally. Use terrain to determine the path rather than drawing a decorative glowing stripe over the scene.

The water should be visible as water, with a relationship to the surrounding land and vegetation.

### Vegetation and luminous distribution

Replace the current impression of scattered glowing dots across an empty basin with distinct ecological regions.

Use the new hero asset library, varied plant clusters, dark terrain, and carefully placed luminous focal areas.

Create meaningful density variation. Some regions should be densely alive; others should remain dark and quiet.

Check the moon's shape, scale, and placement against the actual reference. Do not make a major compositional change simply because a different moon shape is easier to render.

The objective is targeted refinement, not a wholesale rebuild.

---

## Phase 6 — Refine the meadow

The latest meadow has a useful overall landscape arrangement. Preserve it and concentrate on the weak foreground and uniform vegetation.

### Foreground

The large black rocks currently occupy too much visual attention without contributing sufficient material detail or interesting silhouettes.

Improve their scale, surface response, and relationship to the ground. Integrate moss, low vegetation, and selected hero plants.

Use foreground forms to guide the eye toward the stream and distant landscape.

### Vegetation

Use multiple layers of distinct organisms:

* Fine ground-hugging growth.
* Small clusters of luminous plants.
* Medium-height botanical forms.
* Occasional tall hero stalks.
* Dark vegetation that creates contrast.
* Rare bright focal specimens.

Avoid a uniform carpet of identical luminous points.

### Water and horizon

Preserve the existing stream and overall composition while improving reflections, bank geometry, surface variation, and atmospheric transitions.

Maintain the cool twilight palette and the distinction between foreground, middle ground, and distant mountains.

Render the meadow again before undertaking additional environmental detail.

---

## Phase 7 — Create an art-directed ecological distribution system

Once the hero assets are convincing, build the populations around them.

The four compositions should share a coherent ecosystem and asset library, but each must have its own art-directed camera composition.

Use deliberate hand placement for hero specimens and controlled procedural distribution for supporting populations.

Distribution should consider:

* Terrain elevation and slope.
* Distance to water.
* Terrain and vegetation masks.
* Species-specific density and scale.
* Clustering and exclusion rules.
* Foreground framing.
* Local focal areas.
* Distance-dependent detail and density.
* Stable random seeds and instance identities.

Do not scatter every species independently and uniformly across the entire landscape.

Different organisms should occupy different ecological niches. Smaller forms should complement the larger plants rather than compete with them at the same visual scale.

Use Blender Geometry Nodes where they provide a useful authoring workflow, but keep the runtime integration practical. Inspect AV Gen's actual import and instancing capabilities before committing to a production pipeline.

Use deterministic placement so that visual comparisons remain stable between iterations.

---

## Phase 8 — Lighting, atmosphere, and finishing

Do not apply one generic lighting or post-processing treatment to every object.

Develop a coherent lighting hierarchy for each shot:

* Dark environmental fill.
* A dominant cool atmospheric light.
* Selective local light from hero organisms.
* Subtle color reflected from water and nearby surfaces.
* Distance-dependent contrast and color.
* Carefully controlled bright accents.

Evaluate fog, depth-aware haze, volumetric scattering, reflections, ambient occlusion, and depth of field according to what AV Gen actually supports.

If a feature is unavailable, test an artistic approximation before introducing a new rendering feature.

### Bloom and exposure

The brightest highlights may bloom, but the underlying plant structure must remain visible.

Avoid uniform halos around every luminous point. Preserve dark regions, fine silhouettes, and local contrast.

Evaluate exposure, tone mapping, bloom, and color grading together. Do not compensate for weak lighting or poor materials with excessive post-processing.

Inspect the final image at its intended display resolution, not only as a small contact-sheet thumbnail.

---

## Phase 9 — Audio reactivity comes after image quality

First make all four scenes visually convincing with audio reactivity disabled.

Then connect the existing AV Gen audio-analysis, signal bus, modulation, parameter, timeline, and scene systems. Reuse the existing architecture rather than creating a parallel framework.

Design separate behaviors for different timescales:

* Fast transients: localized flashes and small organism reactions.
* Medium changes: gradual plant brightness, gentle organic motion, and water shimmer.
* Slow changes: broad changes in ecosystem activity and illumination.
* Musical phrases: coordinated changes in selected regions or carefully controlled camera movement.

Do not connect every object to the same loudness signal.

The ecosystem should feel as though the music awakens existing life. The composition and visual hierarchy must survive even during the loudest section.

Use smoothing, bounded modulation, distinct attack and decay behavior, and deterministic playback wherever the existing architecture allows it.

The audio response must enhance the image, not turn the scene into a conventional audio visualizer.

---

## Phase 10 — Live and offline quality

Inspect the existing live and offline render paths before making changes.

Use the same scene definition, asset library, camera intent, animation behavior, and audio response wherever possible. Quality settings may differ, but the artistic result must remain consistent.

Evaluate higher-quality offline options such as additional instances, finer detail, more expensive reflections, improved shadows, or better atmospheric sampling only where supported and justified.

Profile on the actual target hardware. Avoid excessive CPU-side instance updates, unnecessary geometry reconstruction, excessive draw calls, and expensive per-object work.

If Blender-generated assets or pre-rendered environmental layers improve quality, test their camera robustness, depth integration, color consistency, and live performance.

Do not assume that full 3D is always superior. Choose the technique that produces the best convincing image for the available runtime budget.

---

## Phase 11 — Visual iteration and acceptance criteria

Use the existing visual critic throughout development.

After every meaningful change:

1. Render the scene at a consistent resolution.
2. Compare it against the corresponding target image.
3. Identify the three largest remaining perceptual differences.
4. Determine which actual system causes each difference.
5. Change only the relevant systems.
6. Render again and retain the comparison.

Produce updated contact sheets and a dedicated macro crop after every major milestone.

Evaluate quality in this order:

1. Composition and silhouette.
2. Hero asset quality.
3. Material response.
4. Lighting and value hierarchy.
5. Foreground, middle-ground, and background separation.
6. Water and reflections.
7. Ecological distribution.
8. Fine detail and particles.
9. Post-processing.
10. Audio reactivity.
11. Runtime performance.

### Acceptance gates

**Gate A — Hero assets:** The macro image contains distinctive, structurally complex organisms that remain interesting under restrained emission.

**Gate B — Riverside:** Water, foreground, vegetation, and atmosphere form a coherent environment rather than disconnected layers.

**Gate C — Valley:** The landscape retains its improved scale and atmosphere while gaining more convincing terrain, water, and ecological richness.

**Gate D — Meadow:** The foreground and vegetation population have meaningful variation and no longer read as a uniform luminous carpet.

**Gate E — Integration:** The four compositions share a consistent material language, lighting logic, and believable ecosystem.

**Gate F — Audio:** The music animates the ecosystem without destroying the static composition's quality.

**Gate G — Performance:** Actual live and offline measurements confirm the costs and limitations of the implementation.

Do not declare success because the code compiles or all intended objects are present. Judge the rendered images.

## Required deliverables

1. A concise diagnosis of the latest comparison.
2. A macro hero-asset test rendered early.
3. A documented choice of native 3D, Blender-assisted assets, or hybrid 2.5D based on actual image results.
4. The approved hero asset library.
5. Updated riverside, valley, macro, and meadow compositions.
6. Before-and-after comparisons at consistent dimensions.
7. A visual-critic report describing both improvements and remaining shortcomings.
8. Live and offline performance measurements.
9. A concise record of technical limitations and the next highest-value improvements.

## Final creative direction

The world should be dark, mysterious, and full of extraordinary organic life. Its beauty should come from carefully designed forms, selective light, rich material detail, layered depth, and deliberate composition.

At first, the ecosystem is quiet. Individual organisms glow softly against deep blue and violet darkness. The water catches isolated reflections. As music begins, small areas of life awaken, luminous clusters respond, and activity gradually spreads through the environment.

The final result should feel like a finished piece of audiovisual environment art created by a skilled VFX artist—not a procedural landscape demo.

**Begin with the latest renders, create the first macro asset immediately, and let visual evidence determine every subsequent step.**
