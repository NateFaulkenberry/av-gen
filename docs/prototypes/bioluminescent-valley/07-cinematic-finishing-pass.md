# Cinematic VFX Finishing Pass

(The owner's spec, verbatim, 2026-10-09, given after Checkpoint D (`artpass/D/D-contact-sheet.png`, 7fbbbc6c). It governs from here. The owner also APPROVED the organism family as the library. Coordinator's notes:
- **Performance:** none this phase. Offline and cinematic techniques are allowed. Live comes later, for a macro subsection.
- **The order is binding:** start with the riverbank water (Deliverable A), and show the water experiments to the owner before the large implementation.
- **Two known engine issues to resolve properly,** per §4.5 and §9:
  - material colour factors above 1 are clamped, so emission tints can only darken (verify the HDR path rather than working around it);
  - a scene that fails to load renders the default scene and still exits 0.
- **Division of work:** the asset artist (Blender) takes authored geometry: the karst tower, the moon, hero clouds, waterfall rock lips and pools, the near canopy, and macro moss and substrate. The lead takes the shaders, rendering, sky, lighting and scene integration.)

# AV Gen — Bioluminescent Ecosystem: Cinematic VFX Finishing Pass

## Mission

We have reached the point where repeatedly trying to match the reference images is producing diminishing returns.

The existing bioluminescent ecosystem is a real scene with a recognizable visual identity. The macro-organism family and meadow have improved substantially. The riverbank and distant vista remain noticeably weaker than the intended result.

**Stop treating this as another reference-matching exercise. Start treating it as a VFX artist's finishing pass on an existing world.**

Keep the original references available throughout the process. They remain important for color, atmosphere, composition, biological detail, and the intended visual quality. However, use them as art-direction references, not as rigid instructions to reproduce every pixel.

The objective is to create a cohesive, beautiful, cinematic environment with distinctive geography, detailed terrain, sophisticated water, rich atmospheric color, sculptural clouds, a convincing moon, and integrated lighting.

We are prioritizing final image quality over real-time performance for this phase. Do not limit your solutions to techniques suitable for real-time rendering. Expensive ray marching, higher sample counts, offline rendering, Blender-authored geometry, advanced shading, and additional rendering passes are all acceptable when they materially improve the result.

Our longer-term objective remains to identify a compelling macro subsection of the environment that can run in real time. That is a separate optimization phase. Do not compromise the current cinematic art pass to meet an unproven frame-time target.

Keep the existing visual critic and its iterative self-evaluation workflow. You have been doing a good job presenting candidate results and critiquing your own art passes. We want to preserve and improve that process.

## 1. Inspect the latest contact sheet and establish the artistic direction

Fetch the latest contact sheet before starting:

D-contact-sheet.png

The three columns are:

* Left: original reference art.
* Middle: previous art pass.
* Right: latest art pass.

Use the latest image as the source of truth for the current state. Do not rely on old screenshots, earlier assessments, or assumptions about changes that have already been made.

### Current artistic assessment

**Macro — acceptable foundation**

The new organism family is a meaningful improvement. The cyan, blue, and emerald forms have a much stronger biological identity than the previous iterations.

Retain the successful assets. Fine-tune density, lighting hierarchy, and the ground between organisms. Add appropriate moss, substrate detail, and transitional vegetation so the organisms feel rooted in an environment rather than arranged on bare ground.

Do not redesign the organism family wholesale.

**Meadow — acceptable foundation**

The glowing chartreuse and emerald vegetation now reaches the foreground, and the distracting black rocks have been removed.

Retain the existing composition and ecological distribution. The major remaining opportunity is to restore the reference's warmer sunset horizon without turning the entire vista pink. Add depth to the terrain and refine the relationship between the cool blue landscape, the luminous meadow, and the warm distant sky.

**Riverbank — major finishing priority**

The river is still too flat and visually simple. Its blue surface and sparse, blobby reflections do not reproduce the rich, broken, luminous violet-and-cyan water in the reference.

The new forest behind the river and the removal of the distracting sparkle are useful improvements. Preserve them where they work.

The water itself now needs a substantially more advanced material and rendering approach, together with better integration between the water, shoreline, surrounding vegetation, reflections, and local illumination.

Do not attempt to solve this merely by increasing the water's base color, adding random noise, or increasing bloom.

**Vista — major finishing priority**

The royal-blue sky is a useful improvement, but the vista still lacks the reference's rich atmospheric color, dramatic karst formations, and convincing separation between foreground and distance.

The right-hand mountain has lost its distinctive tower-like silhouette and now reads as a pale, lumpy mass. Restore a deliberate geological form.

The near slope is too bright and competes with the luminous valley floor. Restore the dark, intricate tree-canopy silhouette from the intended composition.

The moon currently looks like a simple cutout. Replace it with a genuinely dimensional celestial object.

The dashed patches of luminous ground need to feel integrated into the terrain and ecology rather than like disconnected glowing marks.

### Overall preservation rule

Keep the scene, camera compositions, successful organism assets, and existing environment structure.

Do not rebuild the world from scratch. Do not replace the entire scene with a new procedural landscape. Work directly within the existing environment, correcting geometry, extending detail, refining materials, and improving the lighting and rendering where they demonstrably need it.

The scene should become more coherent and more beautiful as a result of this work, not simply more complicated.

## 2. Establish the production workflow

Before editing, inspect:

* The scene and asset files.
* Existing terrain and waterfall geometry.
* The current water shader and material interface.
* The sky, cloud, moon, atmosphere, and fog implementations.
* The lighting and post-processing pipeline.
* Available AOVs, geometry diagnostics, scene inspection tools, and render commands.
* The current visual critic and its reporting format.
* The open Blender session and available Blender MCP tools.

Use the existing critic as the central review tool. Extend its capabilities only when there is a concrete evaluation gap.

Use Blender MCP if it helps produce better terrain, rock formations, waterfalls, cloud geometry, moon geometry, or supporting assets. Blender can be used for authored assets, procedural modeling, geometry evaluation, and reference-driven sculpting.

AV Gen must remain the primary scene and rendering environment. Verify that imported assets preserve their intended geometry, normals, materials, and scale.

Do not assume a particular renderer feature exists. Inspect the implementation and identify what is supported, what can be extended, and what needs a deliberate fallback.

### Iteration protocol

For every significant artistic change:

1. Identify the visible problem.
2. Propose a specific VFX solution.
3. Make the smallest coherent implementation change that can test the idea.
4. Render the affected composition.
5. Inspect the result at full resolution.
6. Compare against the reference and previous render.
7. Use the visual critic to identify remaining issues.
8. Keep, refine, or revert the change.

Use fixed camera framing for comparisons.

Render the riverbank and vista frequently. Render the meadow and macro shots at appropriate checkpoints to ensure the finishing work does not damage the successful results.

Continue presenting the user with candidate final looks and a concise critique of what changed, what improved, and what still needs work.

**Do not spend the next pass measuring performance.** This phase is about visual quality. Record rendering failures and obvious reliability problems, but defer performance optimization.

## 3. Riverbank: develop a genuinely advanced cinematic water shader

This is the highest-priority technical and artistic investigation.

Research how experienced real-time and offline VFX artists render luminous water in dark environments, especially scenes with saturated cyan, blue, violet, and magenta light.

Use these as starting points:

* Evan Wallace's WebGL Water: https://madebyevan.com/webgl-water/
* WebGPU Water implementation: https://github.com/jeantimex/webgpu-water
* Relevant water rendering research covering Fresnel reflectance, screen-space and ray-traced reflections, refraction, caustics, wave simulation, and physically based surface shading.

Look for additional credible implementations, papers, and production techniques. Inspect the source code of relevant examples where licensing permits.

Do not simply copy an implementation. Determine which parts address the specific visual problems in this scene and how they fit AV Gen's existing rendering architecture.

### 3.1 Diagnose why the existing water looks flat

Inspect the current river mesh, topology, normals, material, shader, lighting, and reflection implementation.

Determine whether the current appearance is caused by:

* Insufficient surface geometry.
* A flat or overly uniform normal field.
* Weak or missing Fresnel behavior.
* Inadequate environmental reflections.
* Missing or simplistic refraction.
* Incorrect roughness or specular response.
* Insufficient variation in reflected light.
* Missing interaction with nearby luminous organisms.
* An overly uniform water color.
* Insufficient depth perception.
* Inappropriate bloom or tone mapping.
* A combination of these factors.

Render a few diagnostic views if necessary. Inspect surface normals, roughness, reflection contributions, and any available lighting or material AOVs.

Do not start by replacing the shader before identifying what the existing implementation actually does.

### 3.2 Build a layered water material

Develop a water solution with independently controllable components.

**A. Deep-water absorption and body color**

The water should have a deep blue-violet body color that preserves darkness and depth.

Use depth-dependent color or absorption where the implementation permits. Shallow edges, deeper channels, and areas receiving strong reflected light should not all have the same appearance.

Avoid a uniformly opaque blue surface.

**B. Multi-scale surface normals**

Use several scales of surface variation:

* Broad, slowly changing undulations.
* Medium-scale wave structure.
* Fine ripples.
* Very subtle high-frequency surface detail.

The scales should interact coherently. Do not simply stack several identical scrolling noise textures.

Where appropriate, use procedural wave functions, authored wave fields, normal maps, or simulated heightfields.

Allow the river to respond to its environment and flow direction. A calm stream, a fast channel, and a pool beneath a waterfall should not all use identical surface behavior.

**C. Fresnel and specular response**

Implement or improve a physically motivated Fresnel response so that grazing angles can reveal stronger reflections while more direct views retain the underlying water color.

Use a controlled roughness model. The surface should have sharp, broken highlights in some regions and broader, softer reflections in others.

Avoid a uniformly glossy sheet.

**D. Environmental reflections**

Investigate the strongest reflection approach that is practical for cinematic rendering.

Candidates include:

* Screen-space reflections.
* Planar reflections.
* Environment probes or cubemaps.
* Ray-traced or ray-marched reflections.
* Hybrid reflection methods.

For offline shots, consider a higher-quality reflection path that traces against scene geometry or a suitable acceleration structure if the renderer can support it.

Screen-space reflections alone are unlikely to be sufficient because they cannot reliably reflect objects that are absent from the camera's visible depth buffer.

Use the appropriate method for each shot rather than forcing one reflection technique onto the entire environment.

The water should reflect nearby glowing organisms, the dark canopy, the sky, and selected violet or cyan light sources where their geometry and visibility permit it.

**E. Refraction and depth**

Investigate refraction and depth-dependent absorption where the current implementation can support them.

Use refraction to introduce subtle distortions and help the surface feel like a transparent or translucent medium rather than a painted plane.

Do not overdo refraction. The river is an artistic composition, and the goal is legible, beautiful water rather than maximum optical complexity.

**F. Luminous surface detail**

The reference contains bright, sparkling violet and cyan water. These accents need to be part of the material and lighting design.

Develop several complementary highlight types:

* Small, sharp cyan glints.
* Electric-blue reflections.
* Violet and magenta specular streaks.
* Broad, soft reflected color patches.
* Broken, elongated highlights following the surface orientation.
* Occasional pale highlights where the lighting warrants them.

These features should be driven by surface orientation, lighting, wave structure, and the surrounding environment wherever practical.

Do not generate uniform random glowing dots across the water. Avoid a particle-field appearance.

The result should resemble luminous water catching reflected light, not a flat surface decorated with emission.

If additional stylized emissive accents are necessary to match the reference, integrate them into the shading and surface structure so they remain coherent with the physical reflections.

### 3.3 Implement a cinematic water-rendering mode

We are not constrained to real-time performance for this phase.

Investigate whether the existing renderer can support an optional high-quality water path using some combination of:

* Higher-resolution wave simulation.
* More reflection samples.
* Better reflection geometry coverage.
* Refraction.
* Screen-space or ray-marched light transport.
* Surface caustic approximations.
* Underwater or near-surface light scattering.
* Higher-quality transparency or compositing.
* Additional material and lighting passes.

Do not implement every feature indiscriminately. Prioritize the features that visibly improve the actual riverbank render.

Where possible, expose a quality selection or rendering configuration so the cinematic result does not force every future live scene to use the expensive path.

Keep the shared artistic material controls coherent between the high-quality and any future real-time versions.

### 3.4 Waterfall geometry and integration

Inspect every visible waterfall.

Correct any flat sheets, abrupt edges, implausible connections, floating water geometry, or unnatural terminations.

Use geometry and shading together. Depending on the scene, a waterfall may benefit from layered ribbons, irregular flowing sheets, broken streams, spray, and foam around the impact area.

Avoid making it look like a translucent rectangular plane.

Integrate the waterfall with its source terrain and receiving water. Add appropriate local splashes, ripples, reflected light, and subtle mist.

The waterfall should have a readable flow direction and believable contact with the surrounding rock.

Use Blender to model or refine complex rock lips, ledges, pools, and shoreline transitions if this is more effective than the current procedural geometry.

### 3.5 Shoreline and riverbed

The river should feel embedded in the world.

Refine:

* Irregular bank geometry.
* Dark rocks and moss at the waterline.
* Shallow shelves and deeper channels.
* Transition from exposed substrate to water.
* Reflected cyan and violet light on nearby surfaces.
* Small organisms near the water.
* Variation between wet, dry, matte, and glossy surfaces.

Use actual geometric contact and shading where possible. Avoid simply placing glowing vegetation on a flat bank.

Keep the existing forest background if it works. The river should become the focal point without losing the dark ecological framing.

### 3.6 Required water experiments

Render at least three controlled versions of the same camera composition:

1. Improved multi-scale surface shading and Fresnel response.
2. Improved reflections and luminous highlight structure.
3. The strongest combined cinematic solution, including refraction or other advanced effects if justified.

Keep the camera, composition, and overall exposure consistent enough to make the differences legible.

Choose the best approach based on the actual renders, not implementation complexity.

The desired result is rich, sparkling violet-and-cyan water with depth, reflections, dark channels, and complex highlights. It should be one of the most beautiful elements in the environment.

## 4. Color direction: restore the depth and diversity of the original references

The current palette is too narrow. The problem is not simply that the chosen blue is slightly wrong; the original references contain many interacting color families at different spatial scales and intensities.

Extract color from the actual reference images. Do not estimate the entire palette from a small handful of average pixels.

Analyze the image at multiple levels:

* Overall color distribution.
* Sky and distant horizon.
* Deep shadows.
* Mid-distance terrain.
* Luminous vegetation.
* Water and reflections.
* Small high-intensity accents.

Use region-specific sampling and, if helpful, perceptual color-space analysis. Preserve the original reference files and their exact crop regions so the analysis is reproducible.

The following hex values are **initial art-direction anchors**, not a claim that they are exact measured samples from the source images. Verify and refine them against the actual references.

### 4.1 Deep environmental colors

- Midnight navy: #05091A
- Indigo shadow: #101536
- Royal blue: #20265B
- Violet atmosphere: #33245F

These colors establish the dark foundation and atmospheric depth. Use them in the sky, terrain shadows, distant mountain layers, and selected water regions.

Do not replace all shadow colors with pure black. Cool, saturated shadow color helps the environment retain visual richness.

### 4.2 Bioluminescent colors

- Deep teal: #063F43
- Emerald teal: #087E83
- Electric cyan: #12D8D0
- Luminous blue: #2678FF
- Living emerald: #42C66B
- Chartreuse glow: #B5F36D

Use the darker greens and teals in the body of vegetation and the surrounding terrain. Reserve the more saturated cyan, luminous blue, emerald, and chartreuse for selected organisms and their brightest structures.

The meadow can be greener and more chartreuse than the riverbank, while the macro composition can emphasize cyan, electric blue, and emerald. These are different environments within the same world, not identical palettes applied everywhere.

### 4.3 Water and reflected color

- Deep water: #091D43
- Blue reflection: #1859B7
- Cyan reflection: #19C8D5
- Violet reflection: #7350D8
- Magenta accent: #D34BDB
- Cool specular: #C7E8FF

The water should contain all of these families in different proportions, not a uniform blend of blue and purple.

Cyan reflections should often be narrow and intense. Violet reflections can form wider, broken patches. Magenta should be selective. Pale cool highlights should appear where the surface orientation and lighting justify them.

### 4.4 Sunset and distant atmosphere

- Distant plum: #45254F
- Rose violet: #85406F
- Warm rose: #C96D83
- Peach horizon: #F1A47D

These colors belong primarily to the lower horizon, selected cloud edges, and distant atmospheric scattering.

The horizon must not become a uniformly pink background. Keep the upper sky predominantly deep royal blue and indigo. Introduce the warm tones gradually near the horizon, with irregular transitions and clouds that reflect a mixture of cool and warm light.

Use the warm horizon to create a counterpoint to the cyan and emerald valley. It should feel like residual twilight or distant atmospheric illumination rather than an obvious pink gradient.

### 4.5 Color is a spatial and lighting problem

The existing color-selection attempts appear to be treating color as a material-parameter problem. That is too limited.

The same base color can appear dramatically different depending on exposure, surrounding colors, reflected illumination, atmospheric scattering, emission, tone mapping, and bloom.

Implement the color design across several layers:

1. **Base materials:** the underlying colors of rocks, plants, water, terrain, and clouds.
2. **Direct lighting:** cool moonlight or environmental illumination that establishes form.
3. **Local colored illumination:** cyan, teal, violet, and emerald light from selected luminous organisms.
4. **Atmosphere:** colored distance haze and depth-dependent color transitions.
5. **Reflections:** colors carried onto water, wet rock, vegetation, and other reflective surfaces.
6. **Emission:** the actual light-producing portions of organisms.
7. **Post-processing:** restrained bloom, exposure, and tone mapping that unify the final image.

These layers should complement one another. Do not compensate for a poor material with excessive emission or use post-processing to flatten every region into a single color.

Inspect the existing AgX tone-mapping path and how the renderer handles emission values. Verify the current behavior rather than relying on assumptions about values above 1. If an emissive or lighting value is clamped before it reaches the intended HDR pipeline, correct the actual issue instead of continually adjusting artist color variants.

Do not change global color management blindly. Render controlled color tests and judge the final output, not just intermediate material values.

## 5. Sky: build a cinematic atmosphere, not a flat gradient

The sky needs to become a substantial part of the composition.

Research and implement the best combination of sky shading, atmospheric perspective, cloud geometry, lighting, and post-processing that AV Gen can support.

Relevant techniques to investigate include:

* Rayleigh-like scattering for atmospheric color.
* Mie-like scattering approximations for haze and directional glow.
* Altitude-dependent atmospheric color.
* Volumetric or layered clouds.
* Directional lighting on cloud forms.
* Cloud self-shadowing or suitable approximations.
* Warm horizon scattering.
* Atmospheric depth between distant mountain layers.
* Selective light shafts where the composition benefits.
* A star field integrated into the atmosphere rather than pasted on top.

Do not implement a physically complex atmosphere merely for its own sake. Use it to obtain the desired cinematic image.

### 5.1 Build a layered sky

The sky should have several distinguishable components:

* A deep royal-blue and indigo upper sky.
* Rich blue-violet mid-sky.
* A localized warm rose and peach band near the horizon.
* Cloud masses with cool shadow interiors.
* Selectively illuminated cloud edges.
* A restrained star field where visible.
* Atmospheric depth that integrates the sky with the distant mountains.

The transitions should feel natural and spatially coherent. Avoid a simple vertical gradient that looks like a background texture.

The warm horizon should be concentrated in the lower atmosphere. Preserve the dark blue identity of the upper sky.

### 5.2 Clouds: shape, depth, and light

The current sky needs more convincing clouds.

Determine whether the best approach is:

* Authored volumetric cloud geometry.
* Layered sculptural cloud meshes.
* Procedural volumetric density fields.
* Ray-marched cloud volumes.
* A hybrid approach with high-quality hero clouds and supporting distant layers.

Use Blender to author hero cloud forms if that yields more deliberate silhouettes and more convincing spatial structure.

If the renderer supports volume ray marching, investigate a cinematic-quality cloud path with sufficient samples and meaningful light interaction.

Create cloud structures with:

* Large-scale masses that establish composition.
* Medium-scale billows and lobes.
* Fine edge breakup.
* Deep, cool interiors.
* Selectively illuminated upper and side surfaces.
* Variation in opacity and density.
* Atmospheric integration with the distant horizon.

Avoid cotton-ball clusters, uniformly distributed noise, or a generic procedural cloud texture.

Cloud silhouettes should contribute to the image's visual flow. They should help frame the moon and distant mountains without competing with the foreground.

The lighting should mix cool blue-violet shadow with subtle warm horizon illumination. Do not paint every cloud edge the same pink or cyan.

### 5.3 Moon: replace the cutout

The moon should read as a distant celestial body rather than a flat crescent or circular cutout.

Inspect the existing moon geometry, material, and rendering implementation.

Create a more convincing moon with:

* A genuinely three-dimensional spherical form.
* A coherent illuminated limb.
* A shaded terminator between illuminated and dark regions.
* Subtle surface relief or albedo variation.
* A carefully controlled atmospheric halo where appropriate.
* A believable relationship to the scene's lighting direction.
* Correct depth and scale relative to the mountains and sky.

Use Blender to create a moon mesh and surface if necessary. A carefully designed low-frequency normal or displacement detail can be more effective than noisy craters.

Avoid the appearance of a gray disk pasted onto the sky. The moon should have volume, but it should remain consistent with the stylized environment.

Do not over-sharpen the moon's surface detail or turn it into a photorealistic astronomical photograph that clashes with the surrounding art direction.

If the reference supports a crescent, reproduce the illuminated phase through actual geometry and lighting or a robust shader technique, not an arbitrary crescent-shaped polygon.

### 5.4 Integrate the sky with the environment

The sky cannot be treated as an isolated background.

Its colors should affect:

* The upper surfaces of terrain.
* Distant atmospheric layers.
* Cloud reflections in water where visible.
* Selected reflective surfaces.
* The overall balance between warm horizon and cool foreground.

Use actual lighting and reflections where practical. Use carefully controlled artistic approximations when the renderer does not support the desired effect.

The finished vista should feel like one continuous environment extending from the foreground canopy through the luminous valley into the distant mountains and sky.

## 6. Vista: restore the dramatic geology and visual hierarchy

The vista is still the weakest composition and needs a deliberate art-direction pass.

Preserve the current camera and the successful royal-blue sky direction. Focus on geology, foreground silhouette, environmental depth, and integration of the glowing terrain.

### 6.1 Restore the karst tower

The right-hand mountain must regain its distinctive tower-like form.

Inspect the current terrain geometry and determine whether the silhouette was lost because of mesh smoothing, excessive displacement, lighting, or an overly rounded base mesh.

Use Blender MCP to sculpt or construct a deliberate karst formation if necessary.

The target is a distinctive, tall, vertically articulated limestone-like formation with:

* A recognizable tower silhouette.
* Irregular vertical faces and fissures.
* Erosion-like ledges and recesses.
* A narrow or otherwise distinctive upper profile.
* A coherent connection to the surrounding terrain.
* Lighting that reveals its shape without washing it out.

Avoid a generic pointed mountain, a rounded boulder, or a smooth procedural lump.

Create an actual sculptural landmark. Use other distant formations to support it, not to duplicate its silhouette everywhere.

### 6.2 Improve the near slope and canopy

The near slope is too bright compared with the reference's dark tree canopy.

Restore the intended foreground hierarchy.

The foreground should contain a rich, dark, layered canopy silhouette with enough internal detail to feel like real vegetation, but it should not compete with the luminous valley floor.

Use a combination of authored tree forms, varied canopy silhouettes, branches, foliage masses, and carefully controlled fill light.

Where practical, use real geometry rather than a flat black cutout. At the same time, avoid adding so much detail that the foreground becomes noisy.

The important relationship is:

* Dark, detailed foreground canopy.
* Luminous middle-distance valley.
* Dramatic, darker geological landmarks.
* Atmospheric distant mountains.
* Richly colored sky.

This relationship should remain readable at thumbnail scale.

### 6.3 Make the ground glow part of the terrain

The current dashed patches of ground glow still feel disconnected.

Inspect the underlying placement and material systems.

Integrate luminous patches into the landscape using actual terrain-following geometry, ecological clusters, moss, low vegetation, and subtle material transitions.

Vary the size, shape, density, and intensity of the luminous regions. Avoid identical repeated marks.

Use clusters that follow plausible terrain features, river corridors, sheltered pockets, and vegetation zones.

The glow should feel like living organisms and illuminated ground cover embedded in the world, not decals scattered over the terrain.

Retain dark pathways and unlit regions so the glowing patches have a clear visual hierarchy.

## 7. Meadow: restore the warm horizon without losing the successful vegetation

The meadow is now acceptable as a composition. Preserve the new vegetation and foreground coverage.

The main remaining improvement is the sky and the relationship between the warm horizon and the cool luminous landscape.

Use the palette and sky techniques described above.

Create a gradual but spatially rich transition from deep royal blue through blue-violet into rose and peach near the horizon.

Allow the warm color to illuminate selected cloud edges and the distant atmosphere. Avoid spreading the warm color uniformly over the whole sky or turning the entire vista pink.

Maintain the emerald and chartreuse vegetation as the primary foreground color family. Use cooler cyan and blue accents selectively.

Inspect the meadow's distant horizon, moon, and any unexplained floating green sphere inherited from the baseline. Determine whether the sphere is a legitimate scene element or an artifact. Remove it if it has no intentional artistic role.

The meadow should feel like the same world as the riverbank and vista, but with a different local balance of color and vegetation.

## 8. Macro: preserve the successful organisms and improve their integration

Do not restart the macro-organism work.

The current family is good enough to retain as a foundation.

Make targeted improvements to:

* Ground moss and substrate.
* Contact between organisms and the terrain.
* Dark areas between clusters.
* The brightness hierarchy in the center of the frame.
* Supporting vegetation around the main organisms.
* Local illumination and reflected color.
* Depth separation and composition.

Avoid making the entire frame uniformly bright.

The hero organisms should remain the clearest visual subjects. Supporting vegetation should enrich the environment without competing for attention.

Use the macro shot as a candidate for future real-time work, but do not optimize it yet. Once the cinematic result is established, we can determine whether a subset of the assets and lighting can be rendered at live performance rates.

## 9. Refine the world as a VFX artist would

Beyond the major water and sky work, inspect the entire environment for visible weaknesses.

This is a finishing pass, so look for the details that make a world feel professionally authored:

* Unnatural geometry transitions.
* Flat or overly smooth terrain.
* Repeated rock silhouettes.
* Abrupt terrain intersections.
* Waterfalls that do not meet their source or receiving pools.
* Vegetation that floats or intersects incorrectly.
* Unconvincing shoreline geometry.
* Large surfaces with insufficient detail variation.
* Lighting that flattens the terrain.
* Isolated bright objects with no relationship to the environment.
* Distant structures or formations that feel disconnected.
* Empty regions that need purposeful ecological detail.
* Overly busy regions that need subtraction.
* Repeated assets that are recognizable as duplicates.
* Unintended artifacts or unexplained scene elements.

Use geometry inspection and scene evaluation tools where they can help identify intersections, abnormal surface orientations, disconnected components, or other structural problems.

If the current geometry evaluator cannot detect a specific problem that is clearly visible in this scene, consider extending it with a targeted diagnostic. Do not build a general-purpose geometry-analysis framework unless the need is demonstrated.

Geometry tools should support artistic judgment, not replace it. A mesh can be geometrically valid and still be visually unconvincing.

Use subtraction as well as addition. Remove or simplify elements that weaken the composition.

The goal is not maximum detail. It is coherent, deliberate detail.

## 10. Lighting and final rendering

Once the major water, terrain, and sky changes are in place, perform a dedicated lighting and compositing pass.

Inspect the actual renderer capabilities and current tone-mapping behavior before changing the pipeline.

### Lighting

Create a coherent lighting system:

* Cool moonlit or environmental fill.
* Selective local illumination from luminous organisms.
* Stronger reflected cyan and violet color near the river.
* Restrained warm atmospheric illumination near the horizon.
* Directional light that reveals cloud and mountain form.
* Appropriate shadowing and contact darkening.
* Atmospheric separation between foreground and distant layers.

Do not illuminate every object equally.

### Emission and bloom

Use emission to establish the source of luminous color and bloom to create a restrained halo around bright regions.

Preserve the underlying structure of organisms and water highlights.

Avoid large, uniform glow halos that merge neighboring objects into indistinct masses.

Use multiple scales of luminous detail: sharp glints, smaller glowing organs, broader reflections, and a few soft environmental halos.

### Exposure and tone mapping

Render and compare several controlled exposure and tone-mapping settings.

Ensure the bright cyan and chartreuse accents retain their color and detail rather than clipping to white.

Preserve detail in the darkest terrain and foliage.

Do not assume that a brighter image is a better image. The intended scene derives much of its impact from contrast between darkness, saturated color, and small luminous accents.

### Optional advanced effects

Investigate additional effects only where they improve the image:

* Volumetric mist.
* Local light shafts.
* Surface caustics.
* Water spray and fine droplets.
* Depth of field for the macro composition.
* Selective atmospheric glow.
* Higher-quality reflections.
* Additional compositing passes.

Use higher-quality offline techniques if necessary. Avoid gratuitous effects that obscure the scene's composition.

## 11. Keep the visual critic in the loop

The existing visual critic is a strength of this workflow. Continue using it.

For each major pass, provide:

1. The current reference and candidate renders.
2. A side-by-side comparison with consistent framing.
3. A short critique of the three most important remaining issues.
4. A description of the changes made and their intended effect.
5. A clear assessment of what improved and what did not.
6. The next recommended experiment.

Use the geometry evaluation tools where they are relevant. Add image or scene diagnostics only when they provide useful evidence.

When evaluating the river, specifically ask:

* Does the water have convincing depth?
* Are the reflections spatially coherent?
* Are violet and cyan highlights integrated with the surface?
* Is the water more interesting than the old flat plane?
* Do the shoreline and waterfall feel physically connected?
* Is the water visually strong without simply being overexposed?

When evaluating the vista, specifically ask:

* Does the karst tower have a memorable silhouette?
* Is the near canopy dark enough to frame the scene?
* Does the sky contain distinct color layers?
* Do the clouds have volume and coherent lighting?
* Does the moon look three-dimensional?
* Do the luminous ground patches feel integrated with the terrain?
* Does the composition have a clear focal hierarchy?

Do not allow a generic aesthetic score or a positive self-critique to substitute for inspecting the actual image.

## 12. Required deliverables

Work in the following order.

### Deliverable A — Water shader investigation

Inspect the existing implementation, research the candidate techniques, and render a small set of water experiments.

Show the results before committing to a large implementation.

### Deliverable B — Cinematic riverbank

Deliver a substantially improved riverbank shot with:

* Advanced water shading.
* Rich cyan and violet reflections.
* Convincing surface depth and variation.
* Improved shoreline geometry.
* Integrated waterfalls and surrounding terrain.
* Better local light interaction.

### Deliverable C — Cinematic sky and vista

Deliver a substantially improved vista with:

* A rich blue upper sky and a controlled warm horizon.
* Sculptural clouds with coherent lighting.
* A dimensional moon.
* A restored karst tower.
* A dark, detailed foreground canopy.
* Integrated luminous terrain.
* Improved atmospheric depth.

### Deliverable D — Meadow and macro finishing

Apply only the changes that improve integration, atmosphere, and composition. Preserve the successful work.

### Deliverable E — Final contact sheet

Produce an updated three-column contact sheet:

* Original reference.
* Previous art pass.
* Latest candidate.

Use the same camera framing and comparable crops wherever possible.

Also provide individual high-resolution renders of the riverbank and vista so the water, geometry, cloud structure, and material response can be inspected.

### Deliverable F — Final art critique

Summarize:

* What materially improved.
* What was preserved.
* What was removed or rebuilt.
* Which rendering techniques were added or extended.
* Which issues remain.
* Which shot or subsection is the strongest candidate for future real-time rendering.

Do not spend this phase optimizing frame rate or reporting performance measurements. The priority is a stunning, coherent final image.

## Final direction

We have done enough to establish the world and get the macro organisms and meadow into a reasonable place.

Now make the world beautiful.

Think like a senior environment artist, a lighting artist, a look-development artist, and a VFX supervisor working together. Improve the actual geometry, materials, lighting, atmosphere, and relationships between elements.

Keep the successful scene structure. Keep the references nearby. Do not get trapped in endless attempts to match isolated colors or individual pixels.

Build a sophisticated river shader. Sculpt the terrain and karst formations. Restore the dark canopy. Give the sky depth and warm-cool contrast. Make the clouds dimensional. Make the moon feel like a real object in this stylized world. Make the waterfalls belong to their terrain.

Render early, inspect the result, critique honestly, and iterate.

**Start with the riverbank. It has the largest gap between the current render and the intended visual quality. Research the water-rendering approaches, build a small comparison, and show the best result before moving on to the rest of the finishing pass.**
