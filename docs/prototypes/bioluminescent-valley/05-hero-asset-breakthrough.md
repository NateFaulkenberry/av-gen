# Hero Asset Breakthrough & Production Art Pass

(The owner's spec, verbatim, 2026-10-09. It was given after `artpass/final/sbs-all.png` (c727a200), while the colour pass and the artist's colourway variants were under way. It is the governing spec from here and continues `02-vfx-art-pass.md`. Coordinator's notes on how it maps to work already done:
- **The library exists.** The library (veilcap, filigree, chalice, lanternberry, veilfern, bellspire, glowmat/glowmound, reliquary, lattice, plus scene-support assets) and the method decision (Blender heroes + native ground cover + out-of-focus cards) already exist and were owner-approved. Do not redo them; hold them to this spec's bar instead. In particular, apply the NEW requirement of a neutral-material (no-emission) diagnostic render to every asset, and revise any whose silhouette does not hold up without glow.
- **Performance.** The owner said on 2026-10-09: art first, live optimisation only after the look is approved. Checkpoint E is measure-and-report only, not optimise.
- **Colour.** The colour-matching pass the owner asked for continues within this spec's palette roles: deep blue-violet shadows, teal/cyan primary, selected green accents, occasional pale highlights, plus the reference's own violet and gold accents where the reference has them.)

## Mission

Take the existing bioluminescent ecosystem in AV Gen from an interesting procedural scene to a cohesive, professionally art-directed audiovisual environment.

This is a continuation of the existing project, not a greenfield redesign.

**The immediate priority is to make the underlying organic life visually extraordinary.** We have made progress on composition and the overall environments, but several shots still rely on simple glowing shapes, repetitive silhouettes, uniform ground cover, and lighting effects to create visual interest. More particles, stronger emission, heavier bloom, or greater object density will not solve that problem.

We need better-designed organisms, more sophisticated materials, intentional ecological composition, and a production process that proves each change is improving the image.

The final experience should feel like discovering an unfamiliar living ecosystem at night: deep blue and violet shadows, rich cyan and teal bioluminescence, selective green and turquoise accents, translucent biological structures, luminous filaments, subtle environmental motion, reflective water, and tremendous depth.

It must feel like a place that exists, not a collection of effects placed in a 3D scene.

The environment should be compelling when completely static and silent. Audio reactivity should later make an already-beautiful world come alive.

## 1. Begin by examining the actual current state

Before editing anything:

1. Fetch and inspect the latest version of this image. It is the primary visual reference for the current state of the project:

   `artpass/final/sbs-all.png`

2. Identify the target/reference image on the left and the latest AV Gen output on the right in each comparison.

3. Inspect the current scene implementation, asset library, materials, shaders, lighting, environment distribution, camera configuration, and rendering pipeline.

4. Find the scene-generation and rendering commands already used by this project.

5. Use the existing visual critic application to render, inspect, and compare results. Do not create a parallel critic or another redundant review tool.

Do not rely exclusively on source-code inspection. Render the actual scene and inspect the resulting pixels.

### Produce a short visual diagnosis

For each comparison, record:

* What is already working and should be preserved.
* The three biggest remaining differences from the target.
* Whether each difference comes from geometry, silhouette, material response, lighting, composition, distribution, atmosphere, or post-processing.
* Which single experiment is most likely to improve the result.
* How you will objectively compare the experiment against the current image.

Pay particular attention to these previously observed issues, but verify them against the current image rather than assuming nothing has changed:

* **Macro:** organisms can read as glowing spheres, rings, dots, or simple stems instead of distinctive translucent biological forms.
* **Valley:** the large-scale composition and atmospheric depth are promising, but some terrain and vegetation silhouettes can feel repetitive.
* **Riverside:** the luminous riverbank has potential, but broad, flat purple water and disconnected pale structures can undermine the sense of a coherent place.
* **Meadow:** the overall layout has improved, but uniform luminous ground cover and overly prominent dark foreground rocks can compete with the main composition.

These are hypotheses to test, not a reason to blindly repeat previous work.

## 2. Establish a visual baseline and iteration protocol

Before making changes, save baseline renders for all four compositions:

1. Riverside
2. Valley
3. Macro
4. Meadow

Use the existing cameras, framing, output resolution, and render settings wherever possible.

Create a contact sheet showing the current baseline. Record relevant scene settings and renderer performance so later comparisons are meaningful.

For every significant change:

1. State the visual problem.
2. Identify its probable root cause.
3. Change one coherent group of variables.
4. Render the affected shot.
5. Compare the new render with the baseline and target.
6. Inspect the image at both thumbnail scale and full resolution.
7. Keep, revise, or revert the change based on visible evidence.

Do not batch a dozen untested artistic changes together and then guess which ones helped.

Do not spend hours discussing a proposed approach before rendering anything. After the initial inspection, move quickly to a small, high-value visual experiment.

Keep useful experiments in a dedicated branch or checkpoint so that improvements can be reverted without losing good work.

## 3. Highest priority: create a genuinely distinctive organic asset library

The macro composition is the most important test of whether we can produce convincing biological art. Treat it as a miniature VFX production, not a scattering exercise.

The objective is to create a small library of exceptional, reusable organisms whose silhouettes and material qualities are recognizable even before emission and post-processing are enabled.

### 3.1 Design distinct biological archetypes

Develop approximately six to eight visually distinct organism types. Select forms based on the target references and the capabilities of the existing asset pipeline.

Explore forms such as:

**A. Layered translucent organism**

An organic body made from overlapping thin membranes, curved lobes, folds, and partially transparent tissue. The silhouette should have irregularity and depth. Internal illumination should reveal the structure instead of turning the object into a uniformly glowing blob.

**B. Branching filament organism**

A delicate network of curved filaments, tendrils, and branching fibers. Some filaments should terminate in small luminous nodes, while others remain dark or only weakly illuminated. Branches should have designed rhythms rather than arbitrary noise.

**C. Bell, cup, or trumpet organism**

A flared, folded, asymmetrical biological structure with a readable interior and rim. Use its interior to create a layered light response. Avoid a simple primitive cone with an emissive material.

**D. Suspended bulb or colony**

A connected colony of differently sized translucent sacs, membranes, and connecting strands. The units should have an intentional hierarchy and a shared biological logic, not resemble a string of identical glowing beads.

**E. Broad frond or floating leaf organism**

Thin branching surfaces with visible veins, curled edges, and an elegant silhouette. Let the structure catch light selectively along its edges and veins.

**F. Tall stalk organism**

A slender, articulated organism with a sculptural head or crown, subtle segmentation, and fine branching detail. Avoid the generic mushroom silhouette unless the target genuinely calls for one.

**G. Ground-hugging colony**

A layered patch of moss-like tissue, miniature fronds, translucent nodules, and fine filaments. It must read as a three-dimensional patch of living material, not a uniform carpet of dots.

**H. Signature hero organism**

Create one memorable form that gives this ecosystem a visual identity. It could combine several of the above characteristics, but it must have a clear silhouette, internal structure, and deliberate material design.

These are design directions, not a requirement to make eight variations of the same object. Ensure each type occupies a different visual niche.

### 3.2 Use reference-driven modeling, not primitive accumulation

Study the target macro image closely.

Identify:

* The major outer silhouette of each important organism.
* Its primary and secondary structural forms.
* How thin surfaces connect to thicker tissue.
* Where the brightest features occur.
* Which portions remain dark.
* How its structure overlaps with other organisms.
* How the camera sees through, around, or into it.

Build the forms to reproduce those relationships.

Use Blender, the existing Blender MCP workflow, or another available modeling path if it materially improves shape quality. Inspect the actual available tools and asset-import pipeline before choosing the implementation.

Potential techniques include:

* Curved mesh surfaces and custom profiles.
* Procedural branching with controlled topology.
* Geometry Nodes for repeatable structural variations.
* Sculpted or hand-authored hero meshes.
* Thin layered surfaces with thickness where necessary.
* Curved tubes with tapered radii for biological filaments.
* Custom UVs and vertex attributes for gradients and internal patterns.
* Small amounts of irregularity introduced after the main form is designed.

Use procedural generation to produce controlled variation around an art-directed design. Do not substitute randomness for design.

Avoid relying on spheres, toruses, cylinders, simple cones, or repeated primitives as the main visual identity. These can be useful as hidden construction elements or secondary details, but they must not dominate the final silhouette.

### 3.3 Build and render the first hero asset immediately

Do not model the entire library before checking whether the approach works.

Create one exceptional hero organism and a small supporting group. Place them in a deliberately composed macro scene with:

* A dark, unobtrusive background.
* One primary light direction.
* A suitable backlight or rim light.
* A few supporting organisms at different depths.
* Clear negative space.
* A controlled color palette.
* Enough separation to inspect individual silhouettes.

Render the result before adding complex motion, particles, or audio reactivity.

Produce two diagnostic views if practical:

1. A neutral-material render that reveals geometry and silhouette without emission dominating the image.
2. The intended final-look render with the actual lighting, material response, and restrained post-processing.

The neutral render is essential. If the organism only looks interesting when glowing, the geometry has not been solved.

Compare it with the target macro image. Refine the form until its silhouette and structure are compelling at the intended viewing distance.

Only then use the successful design as the basis for the remaining archetypes.

## 4. Materials: make the organisms feel translucent and alive

Geometry is only half the problem. The materials must communicate different kinds of biological tissue.

Develop a small, coherent material family rather than assigning the same glowing material to every organism.

### Material categories

**Translucent membrane**

Thin, delicate surfaces with color variation across the form. Use transmission, refraction, thickness, or backlighting only where the renderer supports them correctly.

**Gelatinous tissue**

Soft, rounded structures with controlled highlights and depth. The body should retain shape and shading rather than becoming a flat emissive volume.

**Biological filaments**

Thin strands with localized luminous sections, subtle color variation, and a controlled balance between emission and reflected light.

**Veined tissue**

A darker or translucent base with more luminous veins and selected edge highlights. Veins should follow the actual geometry and anatomy.

**Matte organic surfaces**

Dark leaves, stalks, moss, and supporting tissue that establish contrast against luminous features.

**Luminous tips and organs**

Small, selectively bright areas that act as visual accents. Not every tip or node should have identical brightness or color.

### Technical requirements

Inspect the current material and renderer implementation before deciding how to achieve these effects.

Determine what is genuinely supported for:

* Transparency and transmission.
* Thin-surface shading.
* Normal maps and vertex normals.
* Vertex colors and custom attributes.
* Roughness and specular response.
* Emission gradients.
* Subsurface-like shading or approximations.
* Depth sorting and transparency.
* Bloom and tone mapping.

Do not assume the renderer implements a feature merely because the material system exposes a parameter for it.

If true transmission or subsurface scattering is unavailable or too expensive, create an intentional approximation using layered geometry, backlit shading, vertex-color gradients, edge emission, or other supported techniques.

Avoid adding a costly renderer feature solely for one shot unless the benefit is clear and the implementation is justified.

### Emission discipline

Emission is a design accent, not a replacement for material.

Use darker base colors and selectively luminous structures. Preserve dark areas within and between organisms.

Keep bloom restrained enough that thin filaments and small structures remain distinguishable. If bloom destroys their detail, reduce emission intensity or improve the material response instead of increasing the bloom.

Create a palette with clear roles: deep blue-violet shadows, teal and cyan primary illumination, selected green accents, and occasional pale highlights.

Avoid assigning every organism the same cyan gradient. Palette variation should support ecological identity, not become random rainbow coloring.

## 5. Test the asset-production strategy instead of guessing

Once the first hero organism is promising, compare the practical options available to this project.

Test a representative macro composition using:

**Approach A: Native AV Gen assets**

Use the existing engine's procedural meshes, materials, and instancing where these can deliver the required quality.

**Approach B: Authored assets**

Create a small set of higher-quality meshes and materials in Blender, then import and render them through AV Gen.

**Approach C: Hybrid production**

Use authored hero assets for the most visible organisms, native procedural generation for controlled variations and supporting vegetation, and lightweight techniques for distant details.

Compare the approaches using:

* Silhouette quality.
* Material quality.
* Art-directability.
* Asset iteration time.
* Import and shading correctness.
* Live performance.
* Offline render quality.
* Reusability across all four compositions.

Choose based on actual renders and implementation cost.

The likely objective is not maximum procedural purity. It is a practical production workflow that lets us create excellent scenes quickly and render them reliably.

Do not move the entire environment into Blender or introduce a second rendering pipeline without evidence that doing so improves the final result.

AV Gen must remain the actual playback and rendering environment for the production scene.

## 6. Riverside: make the water and banks belong to the same world

Preserve the strongest aspects of the existing composition.

Inspect the current river geometry, material, lighting, reflections, and relationship to the banks. Determine why the water can read as a broad, flat purple surface rather than a dark body of water embedded in a living landscape.

Improve the following where needed:

* A darker, more legible base water color.
* Irregular banks and natural shoreline transitions.
* A coherent waterline that follows the terrain.
* Subtle surface variation at multiple spatial scales.
* Reflections or reflected light that reinforce the environment.
* Broken, elongated highlights instead of one continuous luminous stripe.
* Selective luminous organisms near the water, with their light influencing nearby surfaces where feasible.
* Dark gaps that preserve the perception of water depth.

Do not simply make the water brighter or add a procedural noise texture and call it finished.

First determine which reflection and water-shading capabilities already exist. Use them if appropriate; develop a targeted solution only if the existing implementation cannot produce the required look.

Inspect the pale or geometric structures in the distance. Decide whether they are meant to be natural formations, architecture, or unrelated artifacts. If they do not support the composition or story, correct or remove them.

At the intended camera angle, the scene should have clear foreground, middle-ground, and background layers. Use the river to guide the eye into the scene rather than letting it dominate as a flat colored plane.

## 7. Valley: preserve the composition, improve the ecological design

Do not discard the existing valley composition merely because individual details need work.

Keep the strongest large-scale relationships between the winding landscape, luminous river, distant terrain, and atmospheric depth.

Improve the terrain and vegetation only where the current image shows a clear weakness.

### Terrain

* Vary major silhouettes while preserving a coherent geographic structure.
* Avoid repeated triangular peaks or equally spaced ridges.
* Use overlapping terrain forms to establish distance.
* Give important slopes and ridges a deliberate visual rhythm.
* Avoid excessive surface noise that makes the landscape look procedurally dirty.
* Keep enough dark space that the luminous organisms remain the visual focus.

### Ecology

Use the new hero-asset library to create recognizable ecological clusters.

A valley should not simply contain the same plants at a larger scale. Use different species, densities, and distributions for the valley floor, riverbanks, sheltered pockets, and distant slopes.

Create visual landmarks and quieter passages. Some areas should be densely alive; others should be dark and nearly empty.

Review the moon or other large background features if present, but change them only if their scale, shape, or placement demonstrably harms the composition.

The final image should feel like a single designed environment with a believable visual hierarchy.

## 8. Meadow: eliminate the uniform carpet effect

Retain the existing layout and camera if they are working.

Replace uniform luminous ground cover with a layered ecosystem.

Use a deliberate hierarchy:

* A few large or medium organisms that establish the composition.
* Small clustered vegetation that connects the larger forms.
* Fine filaments and subtle details that reward close inspection.
* Dark patches of exposed ground and shadow.
* Occasional luminous accents that draw the eye.

Vary species, height, silhouette, orientation, and cluster density. Keep a few intentional areas of negative space.

Inspect the large dark foreground rocks. If they are useful framing elements, refine their shape and relationship to the vegetation. If they are merely distracting black masses, reduce, reposition, or replace them.

Avoid solving a sparse meadow by increasing the density of identical glowing plants.

The environment should look rich at full resolution but remain readable when viewed as a small thumbnail.

## 9. Create an ecological distribution system with art direction

Inspect how vegetation and organisms are currently placed.

If placement is based on uniform random scattering, improve it with an art-directable distribution model.

Use the simplest implementation compatible with the existing engine and scene architecture.

Potential controls include:

* Elevation and slope.
* Distance to water.
* Terrain orientation.
* Painted density or placement masks.
* Species-specific preferred regions.
* Cluster size and spacing.
* Exclusion zones.
* Minimum spacing between hero organisms.
* Foreground, middle-ground, and background density.
* Stable seeds for repeatable scene generation.
* Distance-based detail reduction where useful.

Give each organism type an ecological niche. A water-loving filament colony should not be distributed identically to a tall stalk or a ground-hugging mat.

Place the most important organisms deliberately. Use procedural distribution to fill and enrich the composition around those authored focal points.

If Blender Geometry Nodes makes it substantially easier to build and iterate on the asset library, use it for asset authoring or distribution where the resulting assets can be integrated cleanly into AV Gen.

Do not introduce a large new procedural framework merely to scatter a handful of organisms.

## 10. Lighting, atmosphere, and finishing

Only refine the lighting after the major shapes and material responses are working.

The desired mood is dark, mysterious, and deeply atmospheric, with small regions of extraordinary biological light.

Establish a clear lighting hierarchy:

1. A restrained environmental fill that preserves shadow detail.
2. Selective emission from the organisms.
3. Local illumination or approximations where supported.
4. Subtle reflected color from the river and nearby surfaces.
5. Atmospheric depth that separates foreground and distant layers.
6. Limited high-intensity accents for the most important focal points.

Inspect the actual renderer support for volumetrics, fog, ambient occlusion, depth of field, reflections, and bloom before using them.

If a feature is unavailable, use an existing supported alternative or leave it out. Do not invent configuration parameters that the engine does not implement.

Keep the brightest regions limited enough that the eye can travel around the scene. Avoid flattening the entire environment into one glowing mass.

Evaluate exposure and tone mapping as part of the full image. A material that looks beautiful in isolation can clip or lose its color in the final rendered output.

The final image should retain fine detail in both the luminous organisms and the dark environment.

## 11. Motion: subtle life, not constant animation

Do not add substantial animation until the static compositions are convincing.

When the assets are ready, create restrained motion that reinforces their biological character:

* Slow, irregular swaying in flexible stalks and fronds.
* Gentle pulsing in selected translucent structures.
* Subtle motion traveling through branching filaments.
* Occasional changes in the intensity of small luminous organs.
* Minimal water-surface movement where supported.
* Very sparse floating particles only where they add depth or atmosphere.

Different organisms should have different temporal behavior. Avoid synchronizing everything or applying the same sine-wave animation to every object.

Motion should preserve the carefully designed silhouettes. Avoid aggressive rotations, random spinning, and exaggerated bobbing.

Make sure animation is deterministic or reproducible in offline rendering where the engine supports that capability.

## 12. Audio reactivity: make the ecosystem awaken

Audio reactivity is a later production layer, not a shortcut to visual interest.

First prove the scene works with audio reactivity disabled.

Then connect the environment to the existing audio-analysis, signal-bus, modulation, and timeline systems. Inspect their current APIs and conventions instead of creating a parallel audio-reactivity architecture.

Design different responses for different scales and types of organisms:

* Fast transients can trigger brief, selective flashes.
* Medium-timescale energy can influence a slow pulse or membrane intensity.
* Low-frequency energy can affect large organisms or broad environmental illumination.
* High-frequency content can influence delicate filaments and small organisms.
* Longer musical phrases can gradually awaken a region of the valley or meadow.

These are design possibilities, not a mandate to map every audio band to an effect.

Use smoothing, envelopes, sensible amplitude ranges, and varied response times. Prevent flicker, uncontrolled brightness, and constant movement.

Avoid making every organism react to every beat. The world should feel as though the music is stimulating a living ecosystem, not driving a synchronized light show.

If a musical build grows more intense, the world may gradually reveal more of its life. During quieter passages, it should be able to settle back into darkness and stillness.

## 13. Protect live performance and offline quality

This project needs to work as a real audiovisual instrument, not just as a beautiful screenshot.

Before adding costly geometry, transparency, animation, or lighting, profile the relevant scene.

Use existing performance instrumentation and report actual results. Do not assume a feature is cheap or expensive without measuring it.

For the live path:

* Preserve frame-time stability.
* Prefer reusable assets and instancing where supported.
* Avoid unnecessary per-frame CPU work.
* Reduce detail with distance where it does not affect the composition.
* Keep transparency overdraw and excessive light counts under control.
* Profile the actual bottleneck before optimizing.

For offline rendering:

* Preserve the same art direction and recognizable scene.
* Allow higher-quality settings when the renderer supports them.
* Ensure animation and scene state remain reproducible.
* Avoid maintaining two unrelated versions of the environment.

Do not sacrifice the core visual quality to hit a performance target prematurely. First establish the quality target, then measure and optimize the actual expensive features.

Do not rewrite the renderer or introduce compatibility layers unless a specific, demonstrated requirement justifies it.

## 14. Required visual checkpoints

The process is not complete when the code compiles. It is complete when the images improve.

### Checkpoint A — First hero organism

Deliver a macro render with one exceptional hero organism and a few supporting forms.

Include a neutral-material diagnostic and a final-look render.

Do this before investing in the complete library.

### Checkpoint B — Asset family

Show the completed set of distinct organism archetypes against a consistent background, using comparable framing and lighting.

Demonstrate that the organisms have different silhouettes and material identities.

### Checkpoint C — Macro production shot

Place the assets in a composed environment with intentional depth, lighting, negative space, and restrained emission.

Compare the result directly against the macro target.

### Checkpoint D — Four scene comparisons

Render riverside, valley, macro, and meadow with the updated assets and materials.

Create a new contact sheet in the same left-reference/right-render format as the baseline.

Keep the original baseline available so progress is obvious.

### Checkpoint E — Live performance

Measure frame time and GPU/CPU cost for the improved scene at the intended live resolution.

Report the resolution, settings, measured performance, and any remaining bottlenecks.

### Checkpoint F — Audio-reactive sequence

Only after the static shots pass review, create a short sequence demonstrating restrained musical awakening, quiet passages, and a more intense musical section.

Show that the motion supports rather than obscures the art direction.

## 15. Working rules

* Inspect the actual code, assets, and current renders before choosing an implementation.
* Use the existing scene and visual critic workflow.
* Preserve the strongest current compositions.
* Prioritize geometry and materials before density, particles, bloom, and audio effects.
* Do not assume renderer capabilities; verify them.
* Do not generate generic realistic landscapes or unrelated architecture.
* Do not replace designed organic forms with random spheres, rings, or particle clouds.
* Do not scatter identical assets uniformly across the environment.
* Do not overcomplicate the asset pipeline without evidence of benefit.
* Render early and frequently.
* Use stable camera framing for meaningful comparisons.
* Keep useful checkpoints and revert changes that make the image worse.
* Report actual results, not merely completed implementation tasks.
* If a proposed improvement fails visually, say so and try a different approach.

## Definition of done

This art pass succeeds when:

1. The macro shot contains distinctive, convincing organisms whose shapes are interesting without emission.
2. Translucent, filamentary, veined, and matte materials have clearly differentiated visual behavior.
3. The four environments share a coherent biological visual identity without looking repetitive.
4. The river reads as water embedded in a landscape rather than a flat purple surface.
5. The valley and meadow retain their strong compositions while gaining ecological variety and more intentional focal points.
6. The scene remains legible at thumbnail size and rewarding at full resolution.
7. Lighting and bloom enhance the underlying assets instead of disguising weak geometry.
8. Audio-reactive motion feels like the ecosystem awakening naturally.
9. The improved scene has measured performance characteristics suitable for its intended live and offline use.
10. The final contact sheet demonstrates visible improvement against both the original baseline and the supplied references.

**Start now:** inspect the latest comparison, diagnose the largest remaining visual gap, and produce the first hero-organism test render. Do not begin by rewriting the engine or rebuilding the entire environment.
