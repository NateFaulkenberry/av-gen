# Owner's art direction revision (verbatim, 2026-10-23)

Coordinator notes (facts checked 2026-10-23):
- The reference image is `~/Desktop/living prism/new-direction.jpg`, not `82-living-prism/new-direction.jpg` (82-living-prism is the review-media folder on the Desktop). Copy it into the review folder for side-by-side comparisons; it is not committed (provenance unknown, public repo).
- The stems are `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav`, not `audio/feline-footwear-stems/`.
- **Frame target conflict:** §12.2 says the live target is 120 Hz / 8.33 ms. The owner's standing rule (2026-10-19) is 1080p at 30 fps live, ignore 120 Hz, and the Living Prism decisions (03 §14) use 30 fps. The coordinator is asking the owner; until answered, budget against 30 fps and also report frame times against 8.33 ms.
- **Frame target decided (owner, 2026-10-23):** 1080p at 30 fps (33.3 ms), as every live scene; it overrides §12.2's 120 Hz. Timings are still reported against 8.33 ms for reference.

---

# AV Gen — Living Prism: Art Direction Revision & Full R&D Specification

**Scene:** `82-living-prism`  
**Primary visual reference:** `82-living-prism/new-direction.jpg`  
**Audio test material:** `audio/feline-footwear-stems/`  
**Project:** AV Gen, C++23 / WebGPU Dawn / Metal  
**Priority:** Visual quality first, with a strict real-time performance budget  
**Camera:** Static, wide landscape composition

---

# 1. Mission and artistic vision

We are revising the art direction for Living Prism based on the concept art at `82-living-prism/new-direction.jpg`.

This is a revision of the existing scene and the architectural lessons learned during its development, not an instruction to discard the work already completed. Before making changes, investigate the current scene, its architecture, its rendering costs, and the improvements already made. Reuse good work wherever possible.

The new scene should depict a fertile, moss-covered green valley at night, with a winding river leading into distant mountains. Distinct clusters of enormous, brilliantly colored crystals emerge from the riverbanks and surrounding rock formations. Above the valley, a spectacular aurora borealis spreads across a gradient, star-filled sky.

The aurora, crystals, and river must feel like parts of a single visual system. Aurora colors should illuminate the environment, interact with the crystals' translucent interiors, appear in their glowing edges and fractures, and flow into the river's reflections. The result should feel like a long-exposure photograph of an impossible but physically coherent natural phenomenon, with an unmistakably digital, otherworldly quality.

The scene should balance three qualities:

- **Natural beauty:** A believable valley, geological formations, moss, grass, water, mountains, and a coherent night sky.
- **Spectacular color and light:** Vivid, sophisticated color relationships; luminous crystals; sweeping auroral ribbons; and rich reflections.
- **A subtle digital character:** Circuit-like energy pathways integrated into the landscape, potentially responding to music, without overwhelming the natural environment.

This is a carefully composed audiovisual environment, not a procedural landscape showcase, a generic fantasy level, or a collection of disconnected effects.

## The central artistic principle

**The quality of the interaction between aurora, crystals, and river matters more than the quantity of geometry or effects.**

Do not spend the frame-time budget populating the landscape with hundreds of rocks, crystals, plants, particles, or lights. Build a convincing composition with a relatively small number of excellent assets and spend the available rendering budget making the hero crystals and their interaction with the aurora truly beautiful.

The concept art is the artistic benchmark throughout development. We do not need to reproduce every pixel or follow every dimension literally, but the final scene must preserve the qualities that make the reference striking.

---

# 2. Mandatory preliminary investigation

Before implementation, perform a thorough audit of the existing `82-living-prism` scene and the relevant AV Gen rendering architecture.

Do not begin by rewriting the scene or inventing new rendering systems.

Investigate:

- The current scene graph, geometry, materials, lighting, shaders, post-processing, and camera setup.
- Existing crystal meshes, rock assets, vegetation, water implementations, sky rendering, and any related effects that could be reused.
- Existing support for translucency, refraction, reflection, screen-space reflections, emissive materials, volumetrics, bloom, exposure, and color grading.
- Existing environment lighting, image-based lighting, reflection probes, baked lighting, and any available light-propagation techniques.
- The existing audio signal bus, stem analysis, named signals, parameter modulation, MIDI controls, and effect modulation systems.
- The performance profiling and adaptive-quality systems, including their current behavior and known limitations.
- The architectural changes and lessons from the preceding Living Prism work, including any fixes to signal evaluation order, same-frame signal availability, and envelope processing.
- Existing asset libraries and tools, including Blender, available rock and vegetation assets, material libraries, and reusable VFX components.

Use source inspection and profiling rather than relying on documentation alone. Confirm which capabilities actually exist and how they behave in the current build.

Produce a short audit identifying:

1. What can be reused unchanged.
2. What needs modification.
3. What is missing.
4. What can be approximated cheaply rather than implemented physically.
5. The most likely rendering bottlenecks.
6. The recommended rendering architecture for this particular scene.

Do not introduce backwards-compatibility layers or generalized rendering infrastructure unless the existing architecture genuinely requires them. Prefer targeted, maintainable extensions.

---

# 3. Research phase

Research the visual and technical problems before committing to a final implementation.

This is not a request for a generic survey of graphics techniques. Research should lead to specific decisions about how to build this scene within AV Gen's actual capabilities and performance constraints.

Investigate the following areas.

## 3.1 Reference image analysis

Study `82-living-prism/new-direction.jpg` directly.

Inspect the original image at full resolution. If useful, crop the image into separate reference images for:

- Individual crystal formations.
- Crystal tips, edges, fractures, and surface details.
- The aurora ribbons and their gradients.
- The river's shape, color, and long-exposure appearance.
- The valley terrain and moss-covered rock formations.
- The circuitry-like pathways.
- The overall color palette and image texture.

Use crops for detailed visual comparisons during asset creation and iteration.

Analyze the crystals as individual objects rather than treating them as an undifferentiated collection. Identify their silhouette families, facet proportions, internal color variation, edge brightness, surface markings, translucency, and relationship to the surrounding light.

Study the overall composition separately. Identify the image's dominant colors, relative brightness levels, contrast hierarchy, visual focal points, and distribution of detail.

Record a compact visual reference sheet containing annotated crops, palette samples, silhouette observations, and important material characteristics.

Use this reference sheet throughout development.

## 3.2 Crystal appearance and rendering

Research how to create convincing stylized crystalline materials that combine:

- Strong, clearly defined facets.
- Colored transmission through the crystal.
- Luminous edges and fracture lines.
- Fine cracks and speckled inclusions.
- Internal light scattering or an inexpensive approximation of it.
- Reflections of a colorful environment.
- Color variation across different faces and internal regions.

Investigate physically based transmission and refraction, but compare them against more affordable techniques such as view-dependent shader approximations, environment-map sampling, baked internal gradients, procedural fracture masks, and selectively applied emissive details.

The goal is not physical correctness at any cost. It is convincing crystalline depth and luminosity from the intended camera position.

Determine which combination gives the strongest image quality per millisecond on the existing renderer.

## 3.3 Aurora borealis

Research both real auroral structure and established real-time rendering techniques for stylized aurora.

Investigate how to create broad, flowing curtains with:

- Large-scale ribbons and layered folds.
- Smooth color transitions and localized brighter regions.
- Fine vertical striations or filaments.
- Soft, luminous boundaries.
- Depth and perspective consistent with the landscape.
- Natural-looking motion without excessive noise or rapid movement.

The aurora should be integrated into the sky, not look like flat, disconnected ribbons pasted onto it.

Evaluate skybox or skydome shaders, procedural sky rendering, view-space layers, and other inexpensive approaches. Determine which method best fits the existing camera and renderer.

Also research how the aurora's appearance can influence the apparent illumination of the valley and crystals without requiring a large number of dynamic lights.

## 3.4 Long-exposure aesthetics

Research how long-exposure photography produces the characteristic appearance of flowing water, luminous highlights, soft light trails, and bright edges.

Translate those principles into a real-time rendering approach.

Investigate:

- Flowing, temporally smoothed water highlights.
- Directional streaks and elongated reflections.
- Stable luminous trails that do not flicker.
- Exposure and highlight roll-off.
- Bloom and selective highlight diffusion.
- Subtle film grain and restrained highlight halation.
- The trade-offs between genuine temporal accumulation and a single-frame shader approximation.

Do not assume a long-exposure look requires accumulating multiple rendered frames. A stable, view-dependent shader may be both cheaper and more suitable for a responsive live audiovisual scene.

Avoid excessive motion blur or post-processing that obscures the crystalline geometry.

## 3.5 Reference research deliverable

Produce a concise research report containing:

- Relevant visual references and sources.
- The most promising techniques for each major visual feature.
- Expected visual benefits and approximate performance costs.
- The proposed approach for AV Gen.
- Rejected alternatives and the reasons for rejecting them.
- Any architectural prerequisites that must be completed before implementation.

Research should inform implementation, not become an open-ended phase. Once the key uncertainties have been resolved, proceed.

---

# 4. Scene composition and layout

## 4.1 Camera and framing

Use a fixed camera in a wide landscape shot.

The camera should reveal enough of the valley to establish the winding river, hero crystals, surrounding terrain, distant mountains, and aurora simultaneously.

There must be no camera movement, automatic camera orbiting, procedural camera shake, or cinematic fly-through. No camera motion is required for this scene.

Choose the camera position early and optimize the scene around it.

Use this fixed viewpoint to reduce unnecessary geometry, overdraw, lighting work, and rendering complexity. Where appropriate, bake details and use view-dependent approximations.

A shallower valley with less depth is entirely acceptable if it allows the important elements to remain larger, clearer, and more visually impressive.

The scene must read immediately at its intended presentation resolution. The aurora should dominate the upper portion of the composition, the crystals should establish the principal focal points, and the river should lead the eye through the valley toward the mountains.

Avoid excessive empty space, tiny distant crystals, and a landscape so wide that the hero formations lose their impact.

## 4.2 Terrain

Construct a fertile green valley surrounded by moss-covered rock canyons and distant mountains.

The terrain should have enough variation to feel natural without introducing excessive geometry or clutter.

Use a combination of:

- A deliberately shaped valley floor.
- A winding river channel.
- Rock formations that frame the river and provide locations for crystal formations.
- Moss, grass, and restrained vegetation.
- Distant mountain silhouettes that establish depth.
- Selective surface detail and color variation.

Favor reusable meshes, instancing, material variation, baked shading, and other inexpensive methods where appropriate.

The terrain must not become the primary visual attraction. It is the stage on which the aurora, crystals, and river interact.

Use material color, surface roughness, ambient shading, and carefully placed details to make the valley feel fertile and damp at night. Avoid a uniformly green surface or generic procedural terrain noise.

## 4.3 River

Create a single winding river flowing through the valley and receding into the distance.

The river is a major compositional element, not merely a blue strip of geometry.

Its shape should provide a clear visual path through the scene and create a reflective surface that carries the aurora's colors into the lower half of the image.

The river may narrow and simplify toward the distance, but it should remain legible as one continuous waterway.

Do not reproduce the separate waterfall or tributary-like feature visible near the lower-left corner of the reference image. Do not add additional waterfalls.

## 4.4 Crystal placement

Use approximately six or seven visually distinct crystal formations as an initial composition target, with around twenty prominent crystal tips as a reference for scale and visual density.

These numbers are guidelines, not mandatory object counts.

The important requirement is that the crystals remain distinct hero objects. Do not fill the environment with numerous tiny crystal clusters or uniformly distribute crystals across every available surface.

Each formation should have a deliberate silhouette and spatial relationship to the river and terrain.

Vary the height, width, tilt, branching, facet proportions, and arrangement of the crystals. Preserve enough negative space around the formations for their silhouettes and lighting to remain readable.

Cluster the formations along the riverbanks and at selected rock outcroppings. A few crystals may overlap in silhouette where compositionally useful, but avoid making the scene visually congested.

Create a composition diagram showing the camera, river, mountains, and proposed crystal placements before producing final assets.

---

# 5. Hero crystal asset production

The crystals are the highest-priority assets in the scene.

Their silhouettes, materials, and lighting response must receive substantially more attention than background vegetation or decorative details.

## 5.1 Geometry and silhouettes

Create a limited family of distinctive crystal meshes rather than repeating one mesh with simple scale and rotation changes.

Use Blender and existing assets where appropriate. Model or procedurally construct the hero crystals with deliberate artistic control over:

- Main silhouette and proportions.
- Large primary facets.
- Smaller secondary facets.
- Distinctive tips and broken or irregular edges.
- Asymmetric branching and intersections.
- Surface fractures and imperfections.

The geometry should feel crystalline rather than like generic low-poly rocks or regular geometric primitives.

Avoid excessive subdivisions or microscopic geometry that adds cost without contributing visibly at the fixed camera distance.

Inspect the silhouettes in flat lighting and against the actual sky before developing the materials. If the crystals do not look compelling as shapes, shader work will not solve the underlying problem.

## 5.2 Material design

Develop a dedicated hero crystal material or small family of related materials.

The appearance should combine several layers:

**Facet structure:** Large faces should catch light differently, creating clear boundaries between surfaces.

**Internal color:** The crystal interior should appear luminous and translucent, with depth and variation rather than a uniformly emissive surface.

**Glowing edges:** Selected edges should become bright enough to define the silhouette without turning every crystal into a simple neon outline.

**Cracks and fractures:** Thin irregular lines should appear within the crystal, with selective emissive intensity. Fractures should feel embedded in the material, not drawn on top as a uniform wireframe.

**Speckles and inclusions:** Small internal imperfections should add richness and texture, particularly when illuminated by the aurora.

**Environment response:** The crystal should pick up the colors of the sky and surrounding environment. Its appearance should change when the aurora changes, even when the crystal itself is not actively responding to the music.

Use a mixture of modeled features, procedural masks, textures, and shader effects according to their visual importance and performance cost.

Do not make every surface uniformly bright. The contrast between illuminated faces, dark facets, internal glow, and bright fractures is essential to making the crystals feel dimensional.

## 5.3 Translucency and refraction

Evaluate true transmission and refraction against cheaper approximations.

The scene does not require physically exact crystal optics if a carefully designed shader can achieve a similar appearance from the fixed camera.

Consider:

- Fresnel-like edge response.
- View-dependent environment reflections.
- Baked or procedural internal color gradients.
- Distorted environment-map sampling.
- Selective refraction approximations.
- Fake internal depth and layered coloration.
- Precomputed lighting or reflection information where practical.

Avoid rendering each crystal with expensive, overlapping transparent layers unless the visual benefit justifies the cost.

In particular, avoid sorting artifacts, excessive overdraw, and the cost of rendering many overlapping translucent surfaces.

The final material must still look convincingly crystalline, with visible facets and an apparent sense of internal depth.

## 5.4 Crystal and aurora integration

This is one of the most important requirements in the entire project.

The crystals must not look like independently glowing objects placed underneath a colorful sky.

Their materials and lighting should respond to the aurora's palette. When the aurora shifts toward pink or magenta, the crystals should exhibit corresponding reflected or transmitted color changes. When the aurora develops green, blue, or violet regions, those colors should appear in appropriate facets, internal gradients, edge highlights, and reflections.

The response need not be a literal physical simulation. It must be visually coherent and perceptible.

Determine whether this should be implemented through environment sampling, a shared aurora color field, reflection probes, shader parameters, baked lighting, or a combination.

Prefer a shared representation of aurora color and intensity that can drive multiple scene systems consistently rather than independently inventing unrelated color changes in every shader.

---

# 6. Aurora borealis and night sky

The aurora is the second major hero element and the primary driver of the scene's color palette.

## 6.1 Sky composition

Create a gradient night sky with visible stars and a large, sweeping aurora.

The sky should transition naturally from the darker upper atmosphere to a subtle horizon gradient that complements the valley.

Stars should be restrained and remain visible without competing with the aurora. Do not add clouds; the reference clouds are intentionally omitted.

The aurora must occupy a substantial part of the composition. Avoid narrow decorative ribbons that appear as small accents against an otherwise empty sky.

## 6.2 Aurora structure

Build layered auroral structures with large, flowing shapes.

The overall form should be readable from the fixed camera: broad ribbons, curtains, folds, and illuminated bands with smaller internal striations.

Use procedural functions or textures to create coherent shapes. Any noise used should reinforce the large-scale forms rather than generate unrelated visual turbulence.

The aurora should have a clear perspective and appear to extend across the sky above the valley.

Avoid an aurora that looks like a flat wallpaper texture, random translucent triangles, uniform glowing stripes, or a collection of disconnected neon tubes.

## 6.3 Color palette

The aurora should support a rich range of colors, including green, turquoise, cyan, blue, violet, magenta, and pink, as appropriate to the reference palette.

These colors should blend smoothly while retaining enough local contrast to create a spectacular visual effect.

Do not use a uniformly saturated rainbow gradient across every aurora band. Build distinct color regions and transitions with intentional compositional control.

The palette should remain vivid but sophisticated. Preserve dark regions in the sky and within the aurora so the brightest areas have room to shine.

## 6.4 Motion and audio response

The scene uses a static camera, but the aurora may move slowly and organically.

Use restrained changes in ribbon shape, drift, brightness, and color to keep the environment alive. Avoid fast, noisy motion that competes with the music or creates visual fatigue.

Audio modulation should affect selected properties rather than making every aurora feature react independently to every signal.

The aurora should retain its large-scale composition and remain beautiful even when audio input is silent.

## 6.5 Aurora as a lighting source

Investigate how to make the aurora influence the environment without requiring a separate dynamic light for every ribbon or crystal.

Potential approaches include:

- Sampling a shared procedural aurora representation in materials.
- Precomputed environment lighting combined with dynamic color adjustments.
- A small number of carefully selected broad-area or directional lights.
- Approximate colored illumination in the terrain and crystal shaders.
- Selective baked lighting for static environmental features.

Do not assume the aurora must physically illuminate every surface to achieve the desired appearance.

The important result is a consistent visual relationship between the sky, crystals, terrain, and water.

The aurora should appear to cast colored light into the valley, while the crystals and river reinforce its colors back into the composition.

---

# 7. Water shader and long-exposure appearance

The river should be one of the strongest visual elements after the crystals and aurora.

The reference image depicts water with a soft, elongated, luminous appearance reminiscent of a long exposure. Reproduce this character without introducing unstable temporal effects.

## 7.1 Surface appearance

Create water with a combination of:

- Deep, dark base coloration.
- Reflections of the aurora and surrounding terrain.
- Broad, elongated bands of color.
- Fine directional streaks and highlights.
- Subtle variation in surface roughness and flow.
- Selective luminous highlights.
- Smooth transitions between dark water and reflected color.

The water should not resemble a flat mirror, a generic blue ocean shader, or a uniformly emissive ribbon.

Its reflections should stretch and soften in a way that supports the long-exposure aesthetic while preserving the winding shape of the river.

## 7.2 Reflection strategy

Evaluate available reflection techniques, including screen-space reflections, environment maps, planar reflections if appropriate, and shader-based reflection approximations.

A static camera and fixed landscape make precomputed or view-dependent solutions particularly attractive.

Screen-space reflections may contribute useful detail but should not be the sole source of reflection quality if their limitations are obvious at the intended camera angle.

The river should continue to look good when parts of the aurora or crystal formations fall outside the screen-space reflection data.

## 7.3 Aurora reflections

Ensure that the river picks up the major colors of the aurora.

The reflections should follow the river's shape and perspective. Avoid simply copying the sky gradient onto the water without regard for the surface orientation or river geometry.

Use controlled distortion, directional streaking, roughness, and reflection filtering to create a stylized long-exposure look.

The water does not need to reflect every crystal perfectly, but it should provide enough environmental information to connect the composition.

## 7.4 Temporal stability

Test the water while the aurora and audio-reactive elements change.

Reflections and highlights must not flicker, crawl, or shimmer uncontrollably. Avoid temporal accumulation that produces ghosting or smears the crystals whenever their colors change.

If the renderer lacks a suitable temporal solution, prefer stable spatial approximations over expensive frame accumulation.

---

# 8. Circuitry pathways

The circuit-like lines visible in the reference image are a secondary feature.

They add a subtle digital quality to the otherwise natural environment and could provide another channel for audiovisual expression.

## 8.1 Visual design

Create a limited network of luminous pathways that generally follows the valley and river.

The lines should appear integrated with the terrain's composition without making the valley resemble a circuit board.

Use restrained line widths, selected branching patterns, and localized points of brighter illumination.

Avoid a dense, uniform grid or a network that covers the entire terrain.

## 8.2 Rendering approach

Evaluate whether these lines should be rendered as:

- Geometry placed just above the terrain.
- A terrain material layer.
- A composited screen-space or post-processing layer.
- A combination of geometry and a restrained emissive effect.

Choose the approach that best preserves perspective, visual stability, and performance.

The visual result should resemble the reference image, where the circuitry follows the landscape rather than floating arbitrarily over it.

## 8.3 Audio response

If implemented, the pathways may feature restrained pulses, traveling light, localized flashes, or brightness changes synchronized to audio.

Avoid continuously strobing every line. Use selected signals and controlled event density so the effect complements the aurora and crystal response.

This feature is explicitly expendable. If it threatens the performance budget or distracts from the primary elements, simplify or remove it.

---

# 9. Long-exposure post-processing and image character

The concept art has a subtle film-grain quality and bright, softened highlights around luminous features.

Treat these as optional finishing effects rather than prerequisites.

Investigate the existing post-processing pipeline and determine whether the scene can achieve the desired look through:

- Exposure and highlight roll-off.
- Bloom with carefully controlled thresholds and radii.
- Subtle highlight halation or diffusion.
- Gentle color grading.
- Fine, restrained film grain.

The final image should retain the angular structure of the crystal facets and the definition of the valley.

Avoid excessive bloom that turns the crystals into featureless white blobs. Avoid clipping the entire image to saturated colors or crushing the dark landscape into black.

Film grain should be subtle enough that it does not introduce distracting temporal noise in a live performance.

Do not add expensive effects simply because they are common in cinematic rendering. Each effect must produce a visible improvement in this specific scene.

---

# 10. Audio and MIDI modulation

The scene must work as a beautiful static audiovisual environment even before modulation is enabled.

Audio reactivity should enhance the existing visual composition, not replace its art direction with arbitrary motion or random color changes.

Use the existing AV Gen signal bus, timeline, modulation, and parameter systems. Do not build a separate audio engine for the scene.

Use the `Feline Footwear` test material in `audio/feline-footwear-stems/`, with the available synchronized drum, bass, and synth stems.

Analyze the actual stems before assigning signals to visual parameters. Determine the meaningful events, energy envelopes, spectral characteristics, and timing behavior of each stem.

## 10.1 Signal design

Create a modulation map that identifies:

- Input signal or analysis feature.
- Target visual parameter.
- Modulation depth.
- Smoothing and envelope behavior.
- Expected response.
- Relevant MIDI override or palette control.

Use named, meaningful signals where the existing architecture supports them. Verify that all modulation inputs are available in the correct frame and that envelope processing behaves as intended.

Do not introduce redundant analysis paths or new infrastructure if existing features can perform the job.

## 10.2 Suggested mapping

These are starting hypotheses, not mandatory assignments. Refine them after listening to and analyzing the stems.

**Drums**

- Brief increases in crystal edge brightness or fracture emission.
- Subtle, synchronized highlights in selected circuit pathways.
- Small, controlled changes in aurora intensity or luminous detail.

**Bass**

- Slow modulation of internal crystal illumination.
- Gentle changes in the intensity of broad aurora bands.
- Subtle shifts in the river's luminous flow or reflected color intensity.

**Synth**

- Gradual aurora palette shifts.
- Changes in the crystal interiors' color balance.
- Slow variations in auroral shape, width, or brightness.

The three stem roles should remain distinct enough that the user can perceive their contributions.

Do not make the whole scene flash on every beat. The visual response should have a musical hierarchy: transient details for sharp events, slower modulation for sustained energy, and gradual color evolution for harmonic or melodic movement.

## 10.3 Crystal modulation

The crystals should be the principal recipients of high-value audio modulation.

Create a restrained set of parameters that may include:

- Internal luminance.
- Edge and fracture intensity.
- Internal color balance.
- Selected facet highlights.
- Subtle changes in apparent transmission or refraction.
- Localized illumination or energy propagation, where supported.

Avoid scaling, spinning, or deforming the crystals merely to make them appear reactive. Their silhouettes and placement should remain stable.

The visual interest should come primarily from their changing interaction with the aurora and their internal lighting.

## 10.4 Aurora and palette modulation

Integrate the existing MIDI palette functionality with the scene's audio modulation.

Palette changes should propagate coherently through the aurora, crystal materials, water reflections, and selected environmental accents.

Avoid independent random color changes across unrelated objects.

A palette change should feel like a change in the atmosphere of the entire world, while individual crystal formations retain their distinct identities.

Keep the default palette beautiful without user input. Audio and MIDI should expand the expressive range rather than repair an otherwise weak base composition.

## 10.5 Live-channel readiness

Use the existing modulation architecture so the scene can later work with live audio channels as well as offline stems.

Do not require a redesign of the scene's modulation graph when live channels are introduced.

For this development phase, use the `Feline Footwear` stems for repeatable testing, while ensuring the signal mappings remain compatible with the existing live-input workflow.

---

# 11. Asset collection and creation

Perform a focused asset audit before creating new assets.

We already have a substantial asset library, including rock assets and other reusable environmental content. Search the repository and available asset collections thoroughly before modeling everything from scratch.

Use Blender, existing local assets, and suitable free online resources where they improve the result.

Potential assets include:

- Moss-covered rock formations.
- Terrain and vegetation materials.
- Grass and small plants.
- Distant mountain silhouettes.
- Suitable textures for rough, damp, mossy rock surfaces.
- Starfield or sky-related resources, if they offer a genuine advantage.

Do not download assets indiscriminately. Select assets that fit the reference's style, scale, and composition.

For every imported asset, assess whether its geometry and material are appropriate for the fixed camera. Simplify, bake, or modify it as needed.

## Crystal asset creation

Prioritize custom crystal assets over generic environmental asset acquisition.

Build a small, cohesive library of hero crystal meshes with enough silhouette variety to populate the scene without repetition becoming obvious.

If procedural generation helps, use it to generate controlled variations. Avoid relying on a generic random crystal generator without subsequent artistic selection and refinement.

Textures and materials should support the reference's fractured, speckled, luminous surfaces.

## Asset deliverables

Maintain a concise asset manifest recording:

- Asset name and source.
- License or usage restrictions for externally acquired assets.
- Intended scene use.
- Relevant material and texture dependencies.
- Any LOD, baking, or simplification performed.

The final scene must not depend on missing external files or undocumented local assets.

---

# 12. Performance architecture and optimization

Performance is a core design constraint, not a final cleanup task.

The primary objective is to preserve as much frame time as possible for high-quality crystal rendering, aurora integration, and river reflections.

The scene should use the fixed camera to its advantage. Avoid implementing expensive generalized systems where a targeted, precomputed, or shader-based approach can achieve the same visible result.

## 12.1 Establish a baseline

Profile the existing scene before making major changes.

Record CPU and GPU frame times, resolution, frame rate, draw calls, resource usage where available, and the quality settings in effect.

Identify the actual bottlenecks rather than assuming that translucent crystals, reflections, or post-processing are necessarily the dominant costs.

Use repeatable captures and comparable camera and audio states.

## 12.2 Performance budget

Use the existing AV Gen performance target and profiling tools to establish a scene-specific budget.

The known live-render target on the primary development machine is 120 Hz, corresponding to approximately 8.33 ms per frame. Treat this as a reference target, not an assumption that every scene will already meet it.

Allocate budgets across:

- Terrain and environmental geometry.
- Crystal geometry and materials.
- Aurora rendering.
- Water and reflections.
- Post-processing.
- Audio and modulation overhead.

The allocations should be based on measured costs and revised as the scene evolves.

Keep the dominant visual features within a predictable budget. Do not spend all available GPU time on secondary effects and then lower crystal quality to compensate.

## 12.3 Prefer fixed-view optimizations

Because the camera is static, investigate:

- Baked lighting and static shadowing.
- Precomputed reflection information.
- Simplified distant terrain and mountains.
- Reduced detail outside important visible regions.
- Shader-based internal crystal effects.
- Shared material resources.
- Instancing for repeated environmental assets.
- Reduced resolution for selected effects, if visually acceptable.
- Compositing and procedural layers where cheaper than geometry.

Use the most economical approach that produces the desired final image.

## 12.4 Transparency and overdraw

Pay particular attention to crystal translucency and bloom.

Multiple overlapping transparent surfaces can be expensive, particularly when large screen areas are covered by overlapping effects.

Measure the actual cost of crystal materials and prefer approximations that preserve the appearance of depth without requiring unnecessary transparent passes.

Keep the number of hero formations deliberately limited. Do not add decorative crystals simply because the geometry is inexpensive.

## 12.5 Quality scaling

Determine how the scene behaves under the existing adaptive-quality system.

Quality reductions should preferentially remove secondary detail before damaging the scene's core visual identity.

Potential reduction order:

1. Disable optional film grain, halation, and other finishing effects.
2. Simplify or remove circuitry effects.
3. Reduce secondary water and reflection detail.
4. Reduce distant environmental detail.
5. Reduce optional crystal microdetail or secondary shader layers.
6. Lower expensive effect resolution or quality where necessary.

Preserve the primary crystal silhouettes, core materials, broad aurora structure, and essential color relationships for as long as possible.

Document any quality setting that materially changes the scene's appearance.

Do not claim performance success solely because adaptive quality reaches its lowest setting. Report actual frame times at each tested quality level.

---

# 13. Implementation phases and acceptance gates

Work through the following phases in order. Small investigations may overlap where useful, but do not skip the acceptance gates.

## Phase 1 — Audit and R&D

Complete the architecture audit, reference analysis, rendering research, and asset inventory.

**Deliverables**

- Existing scene audit.
- Annotated concept-art reference sheet.
- Rendering-technique comparison.
- Asset inventory.
- Proposed composition and rendering architecture.
- Initial performance budget.

**Acceptance gate:** The team can explain how each primary visual feature will be produced, which systems will be reused, and which performance risks require testing.

## Phase 2 — Composition prototype

Build a rough but fully composed scene using simple geometry and placeholder materials.

Establish the final camera, valley silhouette, river path, mountain background, and crystal placement.

Add a rough aurora representation sufficient to evaluate the overall composition.

**Acceptance gate:** A single screenshot clearly communicates the intended scene. The aurora occupies the sky, the crystals read as distinct hero formations, and the river leads the eye through the valley. The image should already feel close to the reference in its overall color distribution and composition, even before detailed shaders are implemented.

Do not proceed to elaborate asset production if the composition is weak.

## Phase 3 — Crystal asset and material prototype

Develop the first high-quality crystal formation.

Test the geometry, internal illumination, fractures, glowing edges, environmental reflections, and approximate translucency in the actual scene.

Use a representative aurora palette rather than testing the crystal only against a neutral background.

Measure performance.

**Acceptance gate:** At least one crystal looks compelling from the final camera, has clearly readable facets and internal depth, and responds convincingly to aurora color changes. The result must be visibly better than a glowing low-poly object or simple neon geometry.

## Phase 4 — Aurora prototype

Develop the sky and aurora independently enough to evaluate their shape, color, and perspective.

Then integrate the aurora with the crystal material and environment.

Test multiple palettes, brightness levels, and slow motion states.

**Acceptance gate:** The aurora is a major visual feature in its own right and produces meaningful, coherent changes in the crystals' appearance. It must not resemble flat wallpaper or a set of disconnected glowing ribbons.

## Phase 5 — River and environmental integration

Develop the water shader, reflection approach, terrain materials, and selected environmental lighting.

Tune the entire composition together.

**Acceptance gate:** The river reflects and extends the aurora's color into the lower composition. The terrain provides depth and a convincing natural setting without competing with the hero elements. The scene reads as one coherent image.

## Phase 6 — Asset completion and performance optimization

Replace placeholders, refine the crystal formations, complete terrain details, and optimize the scene.

Add circuitry only if it improves the image enough to justify its cost.

Evaluate the optional post-processing effects after the primary image is already successful.

**Acceptance gate:** The full composition meets the agreed visual targets and has measured performance at the target resolution. The core scene remains attractive when optional effects are disabled.

## Phase 7 — Audio and MIDI integration

Analyze `Feline Footwear`, finalize signal mappings, and connect the scene to existing AV Gen modulation and palette controls.

Verify signal timing, smoothing, response ranges, and behavior with silent or low-energy inputs.

**Acceptance gate:** The scene is beautiful without audio, clearly responds to the separate stems, and remains visually coherent during strong musical changes. MIDI palette controls must affect the scene as an integrated color system.

## Phase 8 — Final iteration and regression testing

Perform a final art-direction review against the reference.

Capture comparable screenshots and performance measurements. Fix obvious material, lighting, reflection, composition, and modulation problems.

Test the scene through the normal application workflow and verify that all assets and required resources are available.

**Acceptance gate:** The scene is stable, repeatable, performant within its documented budget, and visually convincing at its intended display resolution.

---

# 14. Iteration and visual quality control

At every major milestone, compare the current render with `82-living-prism/new-direction.jpg`.

Do not rely on memory or verbal descriptions of the reference. Use the image itself.

For each comparison, ask:

- Are the crystals visually distinct and compelling, with recognizable hero silhouettes?
- Do the crystals exhibit the reference's rich colors, glowing edges, internal illumination, fractures, and surface detail?
- Is the aurora large, beautiful, layered, and integrated with the sky?
- Does the aurora's color visibly influence the crystals and surrounding landscape?
- Does the river have the reference's soft, flowing, luminous appearance?
- Is the valley natural, fertile, and sufficiently detailed without overwhelming the composition?
- Is the overall image vivid and colorful without becoming uniformly saturated or washed out?
- Does the image preserve the reference's balance of dark surroundings and luminous focal points?
- Does the result feel handcrafted rather than procedurally generic?
- Are the effects stable and readable at the intended presentation resolution?
- Is the image improving in the actual areas that make the reference compelling?

Use annotated screenshots where useful. Record the principal differences and choose the next iteration based on their visual importance.

Prioritize fixing the largest visible discrepancy rather than adding another feature.

A technically sophisticated implementation is not automatically a successful artistic result.

---

# 15. Explicit exclusions and scope controls

The following are intentionally excluded or deprioritized:

- Camera movement or cinematic fly-throughs.
- Clouds.
- Waterfalls and secondary tributaries.
- Large numbers of small decorative crystal clusters.
- Excessive environmental geometry.
- Physically accurate light transport when an approximation is visually sufficient.
- Expensive real-time translucency or refraction without a demonstrated benefit.
- Elaborate post-processing before the main composition works.
- Circuitry effects that consume significant rendering time without contributing meaningfully to the image.
- Unnecessary generalized renderer rewrites.
- Uncontrolled random animation, rainbow cycling, or visual noise that undermines the composition.

Do not expand the project into a general-purpose procedural landscape generator.

Do not add new visual features merely because they are technically interesting. Every feature must serve the established art direction.

---

# 16. Final deliverables

Provide the following at completion:

1. **Scene implementation:** The revised Living Prism scene, using the existing AV Gen architecture wherever appropriate.
2. **Research report:** The important visual references, implementation decisions, trade-offs, and relevant source material.
3. **Reference sheet:** Annotated crops of the original concept art, including the hero crystals, aurora, water, and overall palette.
4. **Asset manifest:** New and reused assets, their sources, dependencies, and any relevant licensing information.
5. **Modulation map:** The stem and live-channel signal assignments, target parameters, smoothing, and MIDI palette behavior.
6. **Performance report:** Measured CPU and GPU frame times at the target resolution, including the cost of major effects and quality reductions.
7. **Visual comparison:** Representative screenshots of the final scene alongside the reference image, plus any meaningful intermediate comparisons.
8. **Testing report:** Confirmation that the scene loads, renders consistently, responds to the test stems, and works with the normal application controls.
9. **Known limitations:** Any compromises in refraction, reflections, temporal stability, lighting, or other areas that remain visible.
10. **Completion summary:** The major changes, the most important artistic decisions, the performance results, and any remaining work.

Do not report an item as complete unless it has actually been implemented and verified.

---

# 17. Final definition of success

Living Prism succeeds when the static wide shot looks like a spectacular, carefully composed nocturnal landscape in which an immense colorful aurora hangs over a fertile valley, illuminating a small number of extraordinary crystalline formations and reflecting into a winding river.

The crystals must look like genuine hero assets, not generic glowing polygons. Their facets, cracks, inclusions, internal illumination, and environmental response should reward close inspection.

The aurora must establish the scene's atmosphere and color palette. The river must visually connect the sky and the valley. The surrounding terrain must support the composition without distracting from its principal elements.

The entire image should have the luminous, slightly overexposed, long-exposure character of the reference while retaining enough contrast and definition to remain readable during live audiovisual performance.

The result must also be efficient enough that the scene's most important visual features receive the available rendering budget.

**The ultimate test is not how many systems or effects the scene contains. It is whether a viewer immediately recognizes the vivid, crystalline, aurora-lit world envisioned by the reference artist — and whether the world becomes even more compelling when music drives its light and color.**

Use the reference image throughout the process, prioritize the largest visual improvements, and cut anything that consumes meaningful frame time without strengthening that vision.
