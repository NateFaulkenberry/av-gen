# AV Gen Art Direction: Digital Ocean — Simplify the Island and Establish an Anime Background Style

(Owner brief, received 2026-10-15, saved verbatim.)

## Objective

Rework the island in Digital Ocean so it reads as a **small, low, flat island in an illustrated ocean scene**, not a mountainous landscape or a giant mound covered in trees.

The current implementation is overbuilding the landmass. Do not solve this by adding more detail, more trees, more terrain layers, or more elaborate geometry. Solve it through **simplification, silhouette, composition, and color**.

The scene should resemble a carefully art-directed anime background: beautiful, colorful, graphic, and readable at a glance. It should feel like an illustrated world with animated water, clouds, sunlight, dolphins, and boats—not a conventional 3D environment.

## 1. Redesign the island silhouette first

Before touching materials or adding vegetation, strip the existing island back and redesign its overall shape.

**Target silhouette:**
- A long, low horizontal strip of land near the ocean horizon.
- A gently irregular, almost-flat top edge.
- A shallow, understated slope toward the water.
- A few subtle bumps and dips to avoid a mechanically straight shape.
- A relatively thin vertical profile, with the land occupying a small fraction of the image.
- An attractive, asymmetric outline rather than a perfect rectangle or semicircle.

Think of a tropical island as it appears in an illustrated seaside establishing shot—not an aerial view of a mountain, not a volcanic island, and not a giant hill rising out of the sea.

### Strict geometry constraints

- Remove the oversized central mound.
- Do not create a tall peak, mountain ridge, steep hill, or dramatic elevation changes.
- Do not use a large hemispherical or dome-shaped terrain mesh.
- Do not build a substantial foreground landscape that obscures the ocean.
- Keep terrain variation close to the silhouette and visually subordinate to it.
- Prefer a shallow custom mesh, a simple extruded silhouette, or another existing AV Gen technique that produces the desired profile with minimal geometry.
- If the existing terrain generator fights the intended shape, replace or bypass it for this scene rather than adding exceptions and complexity.

The island must remain recognizable as a low island when rendered as a completely flat, single-color silhouette.

## 2. Use a deliberate anime composition

Treat the scene as a wide illustrated background, with the ocean as the main subject.

The island is supporting scenery. It should establish a sense of place without competing with the waves, sun, clouds, dolphins, or boats.

Use these approximate composition targets as a starting point, then adjust against actual screenshots:

- **Sky:** roughly the upper 40–55% of the image.
- **Ocean:** roughly the lower 40–50%.
- **Island:** a narrow band along the horizon, generally only 3–8% of image height.
- **Trees:** a few small silhouettes that rise above the land, with plenty of visible gaps between them.

These are visual guides, not rigid screen-space rules. The final composition matters more than exact percentages.

The island should occupy a modest part of the frame. Do not scale it up simply to make it feel more substantial.

Keep the horizon and island silhouette readable even when the water is animated and the lighting changes.

## 3. Simplify the trees into graphic shapes

The trees should read as deliberate illustrated shapes, not a forest simulation.

Use approximately **3–7 distinct tree silhouettes** across the island as an initial target. Vary their heights and spacing, but do not fill every available gap.

Possible shapes:
- A few small, windswept palms.
- One or two broader rounded tropical canopies.
- A sparse cluster of simple foliage masses.
- A single distinctive taller tree to give the island an identifiable silhouette.

Choose one coherent visual vocabulary. Do not mix realistic conifers, elaborate deciduous trees, palms, and unrelated procedural vegetation styles.

### Tree construction

- Build trees from simple, recognizable silhouettes.
- Use a small number of large foliage masses rather than hundreds of individually visible leaves.
- Use simple trunks and branches only where they improve readability.
- Avoid dense, dark vegetation walls.
- Avoid repeated identical trees with uniform spacing.
- Avoid trees that look like green spheres mounted on poles.
- Do not add individual leaf geometry, detailed bark, or realistic canopy simulation.

Trees should be readable at the intended live-performance resolution. If a tree's internal detail disappears at that scale, remove it rather than trying to preserve it.

A few well-designed silhouettes are better than dozens of generic trees.

## 4. Adopt a specific anime-inspired rendering language

Do not interpret “anime” as merely adding outlines to conventional 3D assets.

The complete scene needs a coordinated illustrated treatment:

- Broad, intentional color regions.
- Strong, clean silhouettes.
- Simplified forms.
- Restrained cel-style or illustrated shading.
- Carefully selected highlight colors.
- Controlled edge softness for atmospheric depth.
- A limited palette with purposeful color relationships.
- Detail concentrated around a few focal elements.

The island should use approximately three principal color values:
1. A main land color.
2. A darker underside or shadow color.
3. A lighter sun-facing accent.

A fourth color may be used for a narrow sandy shoreline if it improves readability.

Avoid photorealistic textures, complex physically based materials, excessive surface noise, and procedural detail that makes the island look like a miniature terrain asset.

Do not outline every edge in black. Use dark navy, deep blue-violet, or a related palette color where an outline or shadow helps the composition.

## 5. Make the shoreline a simple graphic accent

The shoreline should help separate land from water without becoming a detailed beach simulation.

Use one or two restrained techniques:
- A narrow pale sand strip.
- A thin turquoise or mint-colored edge.
- A few small, stylized whitewater accents.
- A simple shadow directly beneath the landmass.

Do not surround the entire island with a thick white foam ring. Do not create a large shelf of exposed underwater terrain. Do not add complex cliffs, boulders, caves, or a jagged rocky perimeter.

The transition from ocean to land should be clean and legible, with a little irregularity to keep it organic.

## 6. Let color and atmosphere create depth

Do not manufacture depth by making the island taller.

Instead:
- Use a slightly darker underside to establish separation from the water.
- Use subtle hue variation across the land.
- Keep distant vegetation simple.
- Allow the island's colors to respond to the scene's overall palette.
- Use a small amount of atmospheric blending where appropriate.
- Keep the horizon clean and the distant scenery low-contrast.

The island should remain visually connected to the sky and ocean rather than looking like a separate, physically rendered asset pasted into the scene.

## 7. Animate selectively

The island itself should remain stable. Animate only elements that benefit from movement.

Potential animation:
- Gentle, small-scale swaying of one or two tree crowns.
- A subtle response to wind.
- Changing warm or cool illumination as the palette changes.
- Animated ocean reflections and waves in front of the island.

Keep the tree movement restrained. No exaggerated bending, constant spinning, or synchronized mechanical swaying.

The terrain must not pulsate, inflate, deform, or move in response to audio.

Audio reactivity should primarily affect the water, foam, highlights, sunlight, and overall color mood. The island is a stable compositional anchor.

## 8. Establish an explicit visual review process

Do not keep iterating on the existing mound. Replace its shape and judge the new design from the actual scene camera.

Produce these three rendered variants:

**Variant A — Minimal silhouette**
- Very flat landmass.
- Three to five simple tree silhouettes.
- One land color, one shadow, and a simple shoreline accent.
- No terrain textures.

**Variant B — Tropical anime background**
- Flat island silhouette.
- Five to seven varied trees.
- Limited cel shading.
- Small sandy or turquoise shoreline.
- A slightly richer illustrated palette.

**Variant C — Graphic travel illustration**
- Extremely simple land geometry.
- Bold, carefully balanced color blocks.
- Trees treated almost like cut-paper silhouettes.
- Minimal shading and almost no surface detail.

Render all three from the same camera, with the same ocean, sun, clouds, and lighting. Compare them at the actual live resolution, not just in a close-up editor viewport.

Choose the variant that best supports the overall Digital Ocean composition. Do not automatically choose the most detailed version.

## 9. Integration and implementation constraints

- Inspect the current Digital Ocean scene and reuse existing scene, material, and rendering infrastructure where appropriate.
- Preserve the intended island position relative to the horizon and other scene elements unless changing it is necessary to fix the composition.
- Do not introduce a new terrain system or a general-purpose vegetation framework for this change.
- Avoid unnecessary dependencies, new rendering passes, and expensive geometry.
- Keep the island inexpensive to render at live-performance resolutions.
- Verify that the island silhouette remains readable during camera movement, palette changes, and water animation.
- Capture screenshots before and after the change and include them in the final report.

## Acceptance criteria

The work is complete when:

- [ ] The island reads as flat and low, not mountainous.
- [ ] The top silhouette is gently irregular without forming a giant mound.
- [ ] The ocean remains the dominant visual element.
- [ ] The island occupies a narrow, controlled portion of the horizon.
- [ ] A small number of trees form attractive, distinct silhouettes.
- [ ] No dense procedural forest or excessive foliage detail remains.
- [ ] The land uses a coordinated illustrated palette and restrained shading.
- [ ] The shoreline is simple and visually clean.
- [ ] The island remains stable while the ocean responds to music.
- [ ] The final result is attractive at the actual live render resolution.
- [ ] The agent includes comparison screenshots and explains which visual variant was selected.

## Final art direction

**Design the island like a shape in an anime background painting, not a piece of 3D terrain.**

A long, low silhouette, a few distinctive trees, a clean shoreline, and beautiful color relationships are enough. Keep the ocean, sky, sun, dolphins, and boats as the stars of the scene.

When in doubt, remove geometry and detail rather than adding more.
