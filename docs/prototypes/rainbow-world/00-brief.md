> Owner's brief, saved verbatim on 2026-10-19. The references are in the owner's Desktop folder
> `~/Desktop/rainbow world/`: clip.mov plus reference images. They are not committed, since their
> source and licence are unknown.

# AV Gen — Rainbow World
## Art Direction, Asset Research, Blender Production, and Visual Development Specification

**Project type:** New audiovisual 3D scene for AV Gen  
**Production approach:** Research-led, artist-directed, iterative environment and VFX production  
**Primary asset workshop:** Blender, using Python automation, headless rendering, and Blender MCP where available  
**Final runtime:** AV Gen's existing C++23 / WebGPU renderer  
**Primary objective:** Create a visually exceptional, continuous, surreal rainbow world that combines a richly colored natural environment, painterly art direction, sophisticated VFX, and disorienting camera movement inspired by the supplied reference animation.

---

# 1. Mission

Create a new AV Gen scene built around a vivid, beautiful, surreal natural world that continuously folds, curves, rolls, and transforms around the viewer.

The environment should contain recognizable natural elements: rich blue skies, clouds, rainbows, lush green grass, rolling hills, distinctive trees, rocks, vegetation, and other details that make the world feel tangible and inhabited.

However, this must not become a generic procedural landscape, conventional cartoon environment, or photorealistic nature scene with rainbow effects layered on top.

The goal is a **carefully art-directed painterly dreamscape elevated by sophisticated visual-effects work**.

The scene should feel like a place created by an imaginative environment artist and a VFX team. Its geometry, surfaces, lighting, atmosphere, colors, silhouettes, and movement must work together as a coherent artistic experience.

The world should be beautiful when viewed as a still image, compelling during slow movement, and extraordinary when the camera travels through its impossible geometry.

## Core requirements

1. Establish a clear and distinctive visual identity before committing to full scene production.
2. Conduct substantial visual and technical research before choosing the final art and implementation approaches.
3. Build the scene piece by piece, giving individual attention to the quality of its major assets.
4. Use Blender as a real asset-production and visual-development environment, not merely as a procedural geometry generator.
5. Discover and evaluate suitable free assets, textures, materials, and production techniques.
6. Create custom assets where they materially improve the scene's artistic identity.
7. Develop the environment through iterative rendering, visual critique, and targeted refinement.
8. Study the supplied animation directly to understand its movement and spatial continuity.
9. Integrate the resulting scene into AV Gen's actual renderer and audio/MIDI signal architecture.
10. Preserve visual quality while meeting the project's real-time performance requirements.

Do not interpret this as a request to immediately generate a complete scene. The first deliverable is an evidence-based art-direction and production dossier, followed by approved visual prototypes and staged implementation.

---

# 2. Inspect the Existing Project and Reference Material

Before beginning visual development, inspect the repository and establish how the new scene should fit into the existing application.

## 2.1 Reference animation

Primary reference:

rainbow world/clip.mov`

Reference art also in rainbow world dir

The reference is a short continuous camera journey through an Escher-like environment. It features a very wide fisheye perspective, folded architecture, spiral staircases, curved surfaces, and figures that remain legible while the camera rolls around them.

The essential reference is the **spatial and cinematic behavior**, not its sandstone architecture, mannequins, or specific materials.

### Required investigation

- Retrieve and inspect the actual video and contact sheet from the repository.
- Confirm the downloaded files and inspect their dimensions, duration, frame rate, and image quality.
- Extract representative frames at regular intervals and around important changes in camera orientation.
- Produce a contact sheet of these frames with timestamps.
- If available, inspect the existing clip-generation or playback workflow to determine whether more precise frame sampling is useful.
- Analyze camera position, orientation, apparent focal length, speed, acceleration, roll, and transitions as far as the footage allows.
- Identify which effects appear to result from camera motion and which require changes in world geometry or spatial arrangement.
- Determine how the camera moves between recognizable spatial configurations without obvious cuts.
- Identify moments of visual clarity, disorientation, and resolution.
- Record uncertainties rather than inventing an exact camera path that cannot be established from the footage.

Do not stop at the written description of the clip. The actual animation is the primary motion reference.

If direct video decoding is unavailable, investigate local tools such as FFmpeg or Blender to extract frames. Do not claim to have watched or analyzed footage that could not be accessed.

## 2.2 Inspect AV Gen

Review the existing engine, scene framework, renderer, available materials, geometry systems, asset importers, camera implementation, lighting, post-processing, audio analysis, MIDI routing, and performance instrumentation.

Determine:

- Which scene types and rendering features are currently supported.
- Which existing scenes contain reusable systems or techniques.
- How external geometry, textures, and environment assets can be imported.
- Whether the engine supports skinned meshes, instancing, indirect drawing, GPU particles, volumetrics, render-to-texture, and the required shader operations.
- How scene parameters connect to the audio signal bus, timeline, modulators, and MIDI controls.
- How scene-specific performance is measured.
- Whether existing scene or asset infrastructure can be extended cleanly.

Inspect relevant source files and documentation before proposing changes.

Avoid unnecessary engine rewrites, duplicate infrastructure, compatibility layers, or speculative rendering systems. Reuse established systems when they are appropriate, but do not compromise the artistic result simply because an existing system is convenient.

---

# 3. Research Before Production

The research phase is mandatory. It must produce concrete artistic and technical decisions.

Do not perform a superficial search, collect a large folder of random reference images, and immediately begin modeling.

Research the work of professional environment artists, look-development artists, lighting artists, technical artists, and VFX artists. Study finished work and production breakdowns that explain how the results were constructed.

## 3.1 Research areas

Investigate the following areas.

### A. Painterly environment art

Study high-quality stylized and painterly natural environments with:

- Deliberately shaped terrain and strong silhouettes.
- Distinctive vegetation and tree canopies.
- Layered color and surface variation.
- Carefully composed negative space.
- Controlled detail density.
- Cohesive materials and lighting.
- Strong readability at different viewing distances.

Look for examples that achieve artistic sophistication without relying on photorealistic scanning or excessive geometric detail.

Research how artists translate visual references into 3D blockouts, sculpted terrain, custom assets, shaders, and final compositions.

### B. Environment composition

Investigate professional methods for composing environments that remain visually compelling as the camera moves.

Study:

- Foreground, middle-ground, and background separation.
- Silhouette and shape hierarchy.
- Scale cues and atmospheric perspective.
- Leading lines, framing, and visual rhythm.
- Landmark placement and visual continuity.
- Controlled repetition and variation.
- How to guide the viewer's eye through a complex environment.

Determine how these principles should change for an extreme fisheye view and a continuously rolling camera.

### C. Surreal and impossible environments

Research Escher-inspired spatial illusions, curved worlds, impossible landscapes, non-Euclidean visual effects, forced perspective, and continuous camera journeys.

Investigate practical production approaches rather than assuming one specific mathematical technique is required.

Potential approaches include:

- A genuinely curved or looping world.
- Carefully designed connected environment sections.
- Localized geometric deformations.
- Camera-relative orientation changes.
- Carefully staged spatial transitions.
- A hybrid of physical geometry and perceptual illusion.

These are candidates for investigation, not predetermined solutions.

The agent must recommend the approach that best reproduces the reference's sense of continuous traversal while remaining artistically convincing and computationally feasible.

### D. Rainbow and luminous-material design

Research the visual representation of rainbows, translucent color bands, atmospheric scattering, luminous surfaces, iridescence, and colored light interacting with nearby surfaces.

Distinguish between a physically simulated rainbow, a stylized atmospheric rainbow, and a deliberately designed luminous arch.

Do not assume that a rainbow should simply be a flat mesh with a rainbow texture.

Explore how geometry, thickness, opacity, roughness, emission, color gradients, atmosphere, and lighting contribute to the desired appearance.

Choose the representation that looks best in the actual camera views and works within AV Gen's renderer.

### E. Clouds, atmosphere, and lighting

Research professional approaches to:

- Large sculptural cloud formations.
- Layered cloud silhouettes.
- Soft cloud shading and internal depth.
- Volumetric lighting and atmospheric perspective.
- Sunlight, shadow softness, and color bleeding.
- Atmospheric haze and depth separation.
- Light shafts and subtle scattering.
- Bright skies that preserve detail rather than clipping to featureless white.
- Effects that remain legible during extreme camera rotations.

Compare geometry-based, shader-based, and volumetric representations. Determine what should be built in Blender, what should be implemented in AV Gen, and what should use a hybrid approach.

### F. Stylized terrain and vegetation

Study sculpted terrain, procedural terrain refinement, art-directed foliage, branching structures, canopy design, grass clumps, and natural asset distribution.

Avoid relying on high-frequency displacement or noise as a substitute for deliberate shape design.

Investigate how to create a limited family of distinctive, reusable assets that provide substantial visual variety without requiring an unreasonable amount of bespoke modeling.

### G. Real-time rendering and VFX

Research approaches for reproducing high-quality environment-art and VFX results in a real-time renderer.

Focus on techniques relevant to the existing AV Gen implementation, including:

- Instancing and level of detail.
- Geometry and material batching.
- GPU-friendly animation.
- Efficient transparency.
- Shader-based atmospheric effects.
- Shadow and reflection trade-offs.
- Efficient foliage representation.
- Texture memory and mipmapping.
- Overdraw and fill-rate constraints.
- Managing GPU workload at high frame rates.

Do not implement every technique encountered during research. Select techniques that address demonstrated needs.

## 3.2 Research deliverable

Create a written research dossier with:

1. Curated references, including direct source URLs and screenshots where permitted.
2. Short annotations explaining what each reference demonstrates.
3. An analysis of relevant production methods.
4. A proposed art direction with a clear rationale.
5. Alternative technical approaches and their trade-offs.
6. Recommended asset sources and licensing considerations.
7. A prioritized list of experiments that will resolve important artistic uncertainties.

The dossier must distinguish observed facts from assumptions and proposed experiments.

Use original artist breakdowns, technical documentation, reputable production articles, and primary sources whenever possible. Search beyond the first page of generic tutorials.

Research should continue during production whenever a particular asset, material, or effect requires additional investigation.

---

# 4. Establish the Art Direction

The visual direction is intentionally positioned between a painterly dreamscape and a sophisticated VFX spectacle.

The result should be vivid, colorful, expressive, tactile, and spatially convincing, with surreal geometry and ambitious camera movement.

## 4.1 World identity

The environment is a lush, imaginative rainbow world.

Its foundational elements include:

- Deep and vivid blue skies.
- Large, softly illuminated white clouds.
- Rich green hills, grass, and vegetation.
- Distinctive trees and foliage.
- Sculptural rocks and natural terrain details.
- Brilliant multicolored rainbows.
- Sunlight, atmospheric depth, and occasional magical-looking environmental effects.

These are starting ingredients, not a requirement to fill every part of the frame with every object.

The scene must have visual hierarchy. Some areas should be open and peaceful; others should contain intricate, layered scenery. Major rainbow structures and terrain formations should provide memorable visual anchors.

## 4.2 Color direction

Establish a documented palette with representative swatches and examples.

The initial palette should emphasize:

- Saturated but nuanced sky blues.
- Varied greens rather than one uniform green.
- Warm sunlight and carefully controlled highlights.
- Natural earth and stone accents.
- Highly saturated rainbow colors used as intentional focal elements.
- Cool shadows that retain color and detail.

Do not make every material rainbow-colored. The contrast between natural environmental colors and vivid rainbow structures is an essential part of the composition.

Avoid indiscriminate saturation, crushed shadows, clipped highlights, and excessive bloom.

The palette should be refined through actual renders rather than chosen exclusively from written descriptions.

## 4.3 Shape language

The world should combine recognizable natural forms with deliberate exaggeration.

Terrain should have broad, designed curves and varied silhouettes. Trees should have distinct masses and branching structures. Rocks should have appealing silhouettes and varied surface character. Clouds should have layered, sculptural forms.

Some elements may be exaggerated in scale or curvature to enhance the surreal quality.

Avoid default primitive-based scenery, uniformly smooth terrain, identical rounded trees, repetitive rocks, and random procedural noise used as the primary source of visual detail.

Procedural tools are welcome when they support controlled, art-directed variation.

## 4.4 Surface and material language

Develop a consistent material system.

Investigate how to combine:

- Broad color variation.
- Subtle roughness and normal detail.
- Painterly surface breakup.
- Natural material transitions.
- Carefully controlled specular response.
- Selective translucency and emission.
- Soft shading that preserves the form of stylized objects.

The target is not a flat, unshaded cartoon aesthetic, nor an indiscriminate collection of photorealistic PBR materials.

Materials should support the world’s shapes and lighting.

Use texture painting, procedural materials, layered shaders, and sourced texture maps where they provide clear value. Avoid unnecessary high-resolution texture detail that disappears in the final camera view.

## 4.5 Lighting and atmosphere

Lighting must be treated as a major component of the art direction.

Develop a clear lighting concept involving a coherent sun direction, ambient illumination, soft shadows, atmospheric depth, and carefully controlled highlights.

Investigate opportunities for:

- Soft sunlight through cloud edges.
- Colored light near major rainbow structures.
- Gentle atmospheric scattering.
- Subtle volumetric beams where compositionally useful.
- Reflected or bounced-looking color.
- Depth separation between terrain, clouds, and distant landscape.
- Controlled transitions between bright open areas and more shaded environments.

Avoid relying on post-processing to rescue weak lighting.

The environment should remain legible and attractive as the camera rolls. Lighting should not become visually confusing simply because the world is upside down.

---

# 5. Asset Strategy: Build, Source, Adapt

Do not build every asset from scratch. Do not source every asset unchanged.

Use an intentional combination of custom assets, curated external resources, and procedural or shader-driven effects.

## 5.1 Class A: Custom hero assets

These define the scene's identity and deserve the most artistic attention.

Expected candidates:

### Hero terrain

Develop a small set of carefully designed terrain formations, including broad rolling hills, ridges, bowls, curved slopes, and potentially impossible-looking transitions.

Create terrain forms that produce compelling silhouettes and camera compositions before adding fine detail.

Use Blender sculpting, custom mesh construction, procedural tools, or combinations of these methods.

Do not assume a generic noise-displaced plane will satisfy the requirement.

### Hero rainbow structures

Design distinctive rainbow forms that are integral to the environment.

Explore curved arches, broad ribbons, nested arcs, looping structures, and other shapes that help frame the landscape or lead the camera through space.

Do not commit to every possible shape. Select a small family based on the actual reference analysis and composition studies.

Develop the material, color distribution, edge treatment, thickness, and illumination as a cohesive look.

### Hero trees

Create a small collection of distinct, art-directed tree silhouettes.

Pay attention to branching, trunk shape, canopy proportions, leaf distribution, color variation, and readability under the intended lighting.

A few excellent trees with controlled variation are preferable to hundreds of generic trees.

### Hero clouds

Develop large cloud forms with distinctive silhouettes and layered depth. Determine whether these should be custom meshes, volumetric structures, shader-based representations, or a combination.

Ensure that cloud forms work in the wide-angle view and during camera rotation.

## 5.2 Class B: Supporting assets

Source and adapt appropriate assets for:

- Rocks and boulders.
- Smaller vegetation.
- Grass clumps.
- Flowers and ground cover.
- Bark, soil, and rock materials.
- Secondary plants and environmental details.

Evaluate assets individually. Reject assets that conflict with the established style, even when they are technically high quality.

Adapt their materials, colors, geometry, and shading where appropriate.

Create controlled variations of selected assets. Avoid uncontrolled random scattering that produces visual noise or repetitive patterns.

## 5.3 Class C: Procedural and shader-driven effects

Investigate native AV Gen implementations for effects such as:

- Atmospheric haze.
- Animated lighting.
- Subtle particles.
- Rainbow illumination.
- Surface or foliage animation.
- Audio-reactive color and intensity.
- Other effects that must update continuously during performance.

Blender may be used to develop the appearance, but the final implementation must be chosen based on the capabilities and performance of AV Gen's renderer.

Do not bake effects into textures if doing so prevents the required animation or audio responsiveness.

## 5.4 Prioritization

For each proposed asset, record:

- Artistic importance.
- Expected screen-space visibility.
- Whether it is a hero or supporting asset.
- Whether it should be custom-built, sourced, adapted, or procedural.
- Modeling and integration complexity.
- Runtime performance cost.
- Licensing requirements, if applicable.

Allocate the most development effort to assets that dominate the image or establish the scene's identity.

Do not spend excessive time on details that will be invisible from the final camera path.

---

# 6. Free Asset Research and Licensing

Actively research available assets rather than assuming everything must be modeled from scratch.

Begin with these resources and investigate additional relevant sources.

### Poly Haven
https://polyhaven.com/

Potential uses include rocks, selected natural models, textures, and HDRIs.

Review the individual asset's suitability and adapt its appearance to the scene.

### ambientCG
https://ambientcg.com/

Potential uses include ground, bark, rock, soil, and other surface materials.

Use these as material inputs rather than assuming their original appearance will match the target style.

### Fab
https://www.fab.com/

Investigate suitable natural assets and relevant free collections. Verify each asset's actual licensing and distribution terms.

### Quixel / Megascans
https://quixel.com/

Investigate availability and applicable licensing. Do not assume historical free-access terms apply to the current account, project, or distribution model.

### Sketchfab
https://sketchfab.com/search

Search specifically for distinctive downloadable models. Verify the license of each selected asset, including attribution and modification requirements.

### Additional resources

Investigate artist portfolios, Blender community resources, open-source procedural tools, Geometry Nodes projects, and detailed environment-art breakdowns.

Use tutorials and breakdowns as technical references even when their associated assets are not available for redistribution.

## 6.1 Asset acceptance

For every external asset:

1. Confirm the actual source URL and creator.
2. Verify that the asset can be downloaded.
3. Verify the license and any restrictions.
4. Record attribution requirements.
5. Inspect geometry, topology, UVs, textures, materials, and scale.
6. Test the asset in a representative scene render.
7. Decide whether it can be used unchanged or requires adaptation.

Do not download large asset collections simply because they are free.

Do not use assets with unclear licensing when a suitable, clearly licensed alternative exists.

Maintain an asset manifest containing the source, creator, license, attribution requirements, local path, and any modifications.

The manifest should accompany the scene's production files.

---

# 7. Blender Production Workflow

Blender is the primary environment for asset creation, look development, and offline preview rendering.

Use both headless Blender and Blender MCP when available.

## 7.1 Headless Blender

Use scripted Blender workflows for repeatable tasks:

- Procedural asset generation.
- Terrain and geometry construction.
- Material creation and batch adjustment.
- Asset import and validation.
- Collection and naming management.
- Turntable renders.
- Isolated asset previews.
- Contact-sheet generation.
- Camera-path previews.
- Repeatable look-development tests.

Keep scripts organized and reusable. Avoid a single enormous script that generates the entire world without intermediate inspection.

Save useful intermediate `.blend` files at significant production milestones.

## 7.2 Blender MCP

Use Blender MCP for interactive inspection and targeted artistic refinement when available.

Useful tasks include:

- Inspecting asset silhouettes.
- Adjusting proportions and branching.
- Reviewing material appearance.
- Refining lighting.
- Correcting composition.
- Inspecting geometry and normals.
- Testing alternate shapes and arrangements.

Do not assume MCP is available or reliable without checking the environment. If it is unavailable, continue with headless Blender, scripts, and rendered previews.

## 7.3 Project organization

Establish a clean asset-production structure inside the repository, following existing project conventions.

A proposed organization is:

- `research/` — reference inventory, source links, research notes, and licensing information.
- `concept/` — palettes, annotated reference boards, and approved visual targets.
- `blender/` — production `.blend` files and reusable Blender scripts.
- `assets/source/` — external assets in their original form where licensing permits.
- `assets/processed/` — adapted and production-ready assets.
- `previews/` — isolated asset renders, look-development images, and contact sheets.
- `manifests/` — asset inventory and licensing records.

Adapt these names to the existing repository rather than imposing unnecessary duplicate structure.

Keep large binaries, downloaded assets, and generated files under appropriate version-control or asset-storage policies.

Do not commit large generated assets indiscriminately.

## 7.4 Asset naming and reuse

Use descriptive, stable names for collections, objects, materials, textures, and export files.

Separate hero assets from supporting assets.

Avoid hidden dependencies on temporary file paths, machine-specific directories, or interactive Blender session state.

Ensure that assets can be regenerated or re-exported through documented workflows.

---

# 8. Mandatory Visual Development and Review

The production process must be iterative.

Do not move directly from research to a complete procedural world.

## Stage 1: Art-direction dossier

Produce:

- Annotated reference board.
- Proposed color palette.
- Shape-language guidelines.
- Material and lighting direction.
- Reference-video analysis.
- Asset inventory.
- Source and licensing plan.
- Proposed production workflow.
- Technical feasibility report.

The dossier must contain specific recommendations, not just a collection of links.

## Stage 2: Isolated asset prototypes

Create initial prototypes for a limited number of major elements:

1. A representative terrain formation.
2. One distinctive hero tree.
3. One representative rock or supporting asset.
4. A hero rainbow structure.
5. A representative cloud formation.
6. A basic lighting and atmosphere setup.

Render assets individually and together.

These are visual-development prototypes, not final assets.

The goal is to establish whether the intended artistic direction works in actual 3D renders.

## Stage 3: Look-development scene

Assemble a small environment using the prototype assets.

Create several controlled camera views, including:

- A wide environmental view.
- A close view of the terrain and vegetation.
- A view dominated by the rainbow.
- A view emphasizing clouds and atmosphere.
- A representative wide-angle view.

Generate a labeled contact sheet.

Compare the results against the research references and written art direction.

Identify the three to five most significant visual weaknesses, then correct those weaknesses before expanding the asset library.

## Stage 4: World and camera prototype

Build a representative section of the world and implement a preliminary camera journey inspired by the reference animation.

Test the world from multiple orientations.

Determine whether the composition remains compelling as the camera moves, rather than only from a single ideal viewpoint.

Evaluate the actual sense of continuity, curvature, disorientation, and spatial depth.

Revise the environment and camera path together.

## Stage 5: Production assets

Only after the prototypes establish a convincing look should the agent expand the environment.

Develop the hero assets to production quality. Add supporting assets and environmental detail according to the established asset plan.

Avoid introducing new visual styles without revisiting the art-direction document.

## Stage 6: Lighting and VFX refinement

Refine lighting, materials, atmosphere, rainbow appearance, and secondary effects.

Evaluate the scene under multiple camera orientations and representative lighting conditions.

Ensure that the effects enhance the underlying environment rather than obscure weak geometry or poor composition.

## Stage 7: AV Gen integration

Integrate the scene into the native engine, using the existing scene and asset systems wherever appropriate.

Test the actual rendered output rather than relying solely on Blender previews.

Differences between Blender and AV Gen are expected. Identify and correct discrepancies in materials, lighting, color, geometry, and effects.

## Stage 8: Final review and optimization

Render representative frames and full camera sequences.

Inspect the scene for visual regressions, missing assets, material errors, lighting problems, repeated geometry, camera clipping, and performance issues.

Optimize the actual bottlenecks while preserving the approved visual identity.

---

# 9. Visual Critique Protocol

Every major review cycle must answer specific questions.

### Composition

- Does the frame have a clear focal point?
- Are foreground, middle-ground, and background visually distinct?
- Are major shapes readable through the fisheye lens?
- Is detail concentrated where it matters?
- Does the composition remain attractive during camera rotation?

### Terrain

- Are the hills deliberately shaped?
- Do their silhouettes have variation and character?
- Are there appealing transitions between terrain forms?
- Does the environment look designed rather than noise-generated?
- Does the terrain support the world-folding concept?

### Vegetation

- Are the trees distinctive at a distance?
- Are their branches and canopies convincingly structured?
- Are foliage colors and materials coherent?
- Is there controlled variation in shape and scale?
- Is the distribution deliberate rather than arbitrary?

### Rainbows

- Do they feel integrated into the environment?
- Are their shapes and proportions visually compelling?
- Are the colors well controlled?
- Do the edges, brightness, and transparency suit the chosen style?
- Do they create useful focal points and framing opportunities?
- Does their appearance survive the actual engine's tone mapping?

### Clouds and atmosphere

- Do clouds have designed silhouettes and sufficient depth?
- Is the atmosphere enhancing spatial separation?
- Are bright regions preserving detail?
- Are volumetric effects improving the image rather than washing it out?
- Does the sky remain visually interesting through the camera journey?

### Materials and lighting

- Are materials cohesive across different asset sources?
- Is surface detail appropriately scaled?
- Are highlights and shadows supporting form?
- Is the color palette intentional?
- Are effects restrained enough to preserve clarity?

### Overall quality

- Does the environment have a distinctive identity?
- Does it look like a considered artistic work rather than a generic generated scene?
- Are there any obvious low-quality or unfinished assets?
- Are any visual effects distracting from the intended composition?
- Does the result remain compelling when paused on a random frame?

The agent must identify concrete problems and implement targeted corrections. Do not treat a successful render or successful test run as proof of artistic success.

Use image comparisons, contact sheets, and repeatable camera views to track progress.

An automated image critic may assist, but must not replace direct visual inspection and explicit art-direction criteria.

---

# 10. World-Folding and Camera-Motion Investigation

The agent must determine how to translate the reference animation's motion language into the rainbow environment.

Do not dictate the final geometric implementation before the reference has been analyzed.

Investigate:

- The relationship between camera translation and camera roll.
- How the apparent direction of gravity changes.
- How the environment occupies the entire wide-angle frame.
- How landmarks recur across different orientations.
- How spatial transitions are concealed or made visually convincing.
- How to preserve a sense of continuous movement.
- How to avoid empty backgrounds and visible world boundaries.
- How the motion interacts with the placement of rainbows, terrain, trees, and clouds.

Test candidate approaches using a small prototype before committing to the full world.

The final solution may combine custom geometry, connected environment sections, carefully staged camera motion, and localized deformations.

The priority is the visual result: a continuous, immersive world whose geometry and orientation feel impossible without appearing arbitrary or incoherent.

The rainbow world must have its own artistic identity. Do not recreate the reference's sandstone architecture or mannequins.

---

# 11. Audio and MIDI Integration

Audio reactivity must eventually be integrated into the scene, but it should not be used as an excuse to skip visual development.

First establish a compelling baseline animation that works without incoming audio.

Then identify which environmental elements should respond to music.

Potential targets include:

- Rainbow brightness and color intensity.
- Cloud illumination and motion.
- Subtle terrain or environmental pulses.
- Foliage movement.
- Particles and atmospheric effects.
- Camera energy and selected motion accents.

These are candidates for design, not a requirement to animate every element.

Use AV Gen's existing audio-analysis and signal-routing systems. Inspect the available signals and modulation mechanisms before designing new infrastructure.

Audio responses should be purposeful, bounded, and visually integrated. Continuous motion should remain smooth, while stronger musical events may produce more noticeable changes.

Avoid arbitrary object spinning, indiscriminate scaling, excessive flashing, and unrelated effects triggered by every transient.

Ensure that the scene remains visually appealing when audio and MIDI are inactive.

Detailed audio mapping and final performance behavior can be developed after the environment's baseline animation is established.

---

# 12. Performance and Technical Constraints

The scene must work within AV Gen's real-time rendering architecture.

The existing renderer uses C++23, WebGPU/Dawn, Metal on Apple Silicon, and an RGBA16Float rendering pipeline with AgX tone mapping. Inspect the current implementation before relying on these details or assuming specific rendering features are available.

The primary development environment is Apple Silicon.

The scene must be evaluated at the actual intended performance resolution and with representative audio/MIDI activity.

Use the existing performance instrumentation to measure:

- CPU frame time.
- GPU frame time.
- Draw calls and state changes where available.
- Geometry and texture memory.
- Transparency and overdraw.
- Shadow and lighting costs.
- Particle and volumetric costs.
- Scene-update and animation costs.

Use a baseline performance capture before major optimization work.

Investigate instancing, level of detail, simplified distant geometry, shader optimization, texture memory management, and alternative effect representations where profiling demonstrates a need.

Do not implement a costly effect merely because it looks good in an offline Blender render.

Conversely, do not remove defining visual characteristics before investigating cheaper ways to preserve them.

Where Blender and AV Gen use different rendering capabilities, explicitly document the intended approximation and validate it in the target renderer.

---

# 13. Production Artifacts and Documentation

Maintain an organized record of the artistic and technical decisions made during development.

Required artifacts:

- Research and reference dossier.
- Annotated art-direction board.
- Color palette and material guidelines.
- Asset inventory and licensing manifest.
- Blender project files.
- Reusable Blender generation and export scripts.
- Isolated asset renders.
- Look-development contact sheets.
- Camera-path preview renders.
- AV Gen integration screenshots or rendered frames.
- Performance captures.
- A record of important artistic decisions and rejected approaches.
- Final implementation and testing notes.

Document why major assets were custom-built, sourced, or generated procedurally.

Keep research findings actionable and concise. The purpose of documentation is to make the work reproducible and explain decisions, not to create paperwork for its own sake.

---

# 14. Acceptance Criteria

The scene should not be considered artistically complete merely because it compiles, loads, or runs at an acceptable frame rate.

## Art direction

- The environment has a distinctive, coherent painterly-dreamscape aesthetic enhanced by sophisticated VFX.
- The color palette is deliberate and consistent.
- Major terrain, tree, rainbow, and cloud assets have recognizable artistic character.
- The environment avoids generic procedural scenery and visibly unfinished assets.
- External assets are selected and adapted to fit the established look.

## Environment quality

- The landscape has deliberate composition, strong silhouettes, and spatial depth.
- Supporting details provide richness without overwhelming the focal elements.
- Rainbows are visually integrated into the world.
- Lighting and atmosphere improve depth, color, and form.
- The scene is compelling in both wide shots and closer views.

## Motion

- The camera journey reflects the reference's continuous traversal and changing sense of gravity.
- The environment remains spatially coherent enough to be understood while still producing disorientation.
- Major landmarks and environmental features contribute to the movement.
- The animation does not depend on arbitrary spinning or disconnected transitions.

## Production quality

- The asset workflow is reproducible.
- External assets have documented sources and licenses.
- Blender files and scripts are organized and usable.
- Visual development includes actual renders and documented refinement.
- The final scene is validated in AV Gen's renderer.

## Performance

- Performance has been measured in the target runtime.
- Major bottlenecks have been investigated.
- Optimizations preserve the scene's defining visual qualities as far as practical.
- Final performance results are documented against a measured baseline.

Where a criterion cannot be met, document the specific limitation, its impact, and the alternatives investigated. Do not silently declare the requirement satisfied.

---

# 15. Required Initial Deliverable

Begin with research and planning, not full implementation.

Produce a concrete proposal that includes:

1. Findings from direct inspection of `clip.mov`.
2. A curated set of high-quality visual references with annotations.
3. A proposed art direction and rationale.
4. A first-pass palette and material language.
5. An asset breakdown distinguishing custom hero assets, sourced supporting assets, and engine-native effects.
6. A shortlist of free asset sources and an approach to licensing verification.
7. A proposed Blender workflow and repository structure.
8. A plan for the initial isolated asset prototypes.
9. A look-development and camera-prototype plan.
10. A technical assessment of the proposed assets and effects in AV Gen.
11. A staged production schedule with dependencies and explicit acceptance criteria.
12. The most important artistic uncertainties and the experiments needed to resolve them.

For every major artistic decision, explain what evidence supports it and what remains uncertain.

Do not begin large-scale environment construction until the initial research and prototype plan is ready.

However, do not let research become an indefinite phase. Once the dossier and initial experiments establish a credible direction, proceed to prototype production, render the results, critique them, and refine the work.

---

# 16. Working Principles

Throughout the project:

- Act as an environment artist, look-development artist, VFX artist, and technical artist working together.
- Research specific artistic problems deeply enough to make informed decisions.
- Prefer strong, intentional compositions over large quantities of generic detail.
- Build signature assets with individual care.
- Source and adapt supporting assets intelligently.
- Use procedural methods to support art direction, not replace it.
- Evaluate real rendered images at every significant stage.
- Correct identifiable visual weaknesses before expanding the scene.
- Validate artistic decisions in AV Gen rather than assuming Blender renders guarantee success.
- Optimize measured bottlenecks without casually sacrificing visual quality.
- Reuse existing infrastructure where appropriate and avoid unnecessary architectural changes.
- Record important decisions and preserve reproducible intermediate results.

**The standard is not “a functional 3D rainbow landscape.” The standard is “a visually distinctive, carefully art-directed world that feels like a finished piece of VFX-driven environment art and works as a live audiovisual instrument.”**

Start by inspecting the repository and reference animation, then produce the research and art-direction dossier. Bring concrete visual evidence and a prioritized production plan before committing to full scene construction.
