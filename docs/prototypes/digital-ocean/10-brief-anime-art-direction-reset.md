# AV Gen Art Agent Brief — Digital Ocean: Anime Art-Direction Reset

(Owner brief, received 2026-10-16, saved verbatim. Supersedes 08-brief-island-simplification.md where they differ: the island is now a small, recognisable island with one distinctive tree and a few rocks.)

## Mission

The latest Digital Ocean iteration has some promising elements, particularly the dolphins, boats, sunset colors, and sun. However, the overall scene still reads as conventional 3D graphics with stylized colors, rather than a deliberately illustrated anime environment.

Perform a focused art-direction reset. Preserve and improve the successful elements, but substantially redesign the water rendering, composition, island, sunlight, and clouds.

**Do not simply polish the current image. Change the underlying visual language.**

The target is a beautiful, mature, illustrated anime ocean scene: a tiny island with a distinctive tree, attractive rocks, expressive stylized water, a luminous sunset, elegant clouds, and a few carefully placed boats and dolphins.

It should look art-directed and memorable in a still frame, with animation enhancing the image rather than being responsible for making it interesting.

## 1. Start by studying the existing project and assets

Before making changes:

1. Inspect the current Digital Ocean scene and capture a baseline screenshot.
2. Find the existing rock assets and assets used by the Tree of Life floating-island scene.
3. Inspect the actual models, materials, silhouettes, and any existing vegetation assets. Reuse the best candidates.
4. Inspect the current water shader, its geometry, lighting, reflections, foam generation, and any screen-space effects.
5. Study the existing Digital Alpine scene for useful composition or rendering techniques, but do not blindly copy its art style.
6. Review the original toon-water reference:
   https://roystan.net/articles/toon-water/

The tutorial is a technical and visual reference for graphic water created using depth-based colors, animated surface patterns, thresholded wave shapes, and foam. Study its actual rendered result and techniques. Do not interpret the assignment as simply making the existing physically shaded water purple.

Do not begin by adding more scene objects. First establish what needs to change in the image.

## 2. Establish the composition around a tiny island

The current framing gives too much visual weight to the water and too little to the island. The horizon is high enough to leave an enormous expanse of foreground water, while the island is barely more than a thin strip.

Recompose the scene around a memorable, small tropical island.

### Island design

Create a compact, low island with:
- One distinctive tree as the primary silhouette.
- A few thoughtfully arranged rocks, including some near the shoreline.
- A simple, gently irregular land silhouette.
- A small amount of vegetation or ground color variation if needed.
- A clean, readable boundary between land and ocean.

The tree should be the recognizable feature of the island. It might be a stylized palm or another distinctive, elegant tree, but it must have an intentional silhouette and believable proportions.

Do not cover the island with a forest. Do not create a large hill or mountain. Do not create an elaborate fantasy landscape.

The island should evoke the classic tiny-island composition: one little piece of land, one beautiful tree, a few rocks, and an immense, colorful ocean and sky surrounding it.

### Reuse existing assets

Use the rock assets already available in the project and investigate the Tree of Life scene for suitable tree, rock, or floating-island components.

These assets are starting points, not mandatory prefabs. Adapt their scale, materials, silhouettes, and shading to the new scene.

Do not create replacement procedural rocks or trees if an existing asset already provides a better result.

If a Tree of Life asset is too detailed or has unsuitable materials, simplify or restyle it. Do not import the entire floating-island environment just because one of its components is useful.

### Composition targets

Explore these starting proportions from the actual scene camera:

- Sky: approximately 40–50% of the frame.
- Ocean: approximately 35–45%.
- Island and its silhouette: visually substantial enough to recognize immediately, with the tree clearly visible.
- Sun: large and visually prominent, with its reflection leading into the foreground.
- Boats and dolphins: supporting focal points distributed across the scene.

These are composition guides, not rigid requirements. The critical change is that the island must no longer disappear into the horizon.

Create three camera-composition variants with different horizon heights, island scales, and island positions. Compare the actual renders before choosing one.

Avoid centering every element. Let the island, sun, and reflection form a deliberate compositional relationship.

## 3. Redesign the water shader from its visual fundamentals

This is the highest-priority technical and artistic task.

The current water still has the appearance of a lit 3D surface with numerous realistic-looking reflections and ripples. The purple palette is attractive, but it does not make the surface look illustrated.

Do not merely adjust the existing water color, roughness, or specular intensity. Investigate whether the current material and shader architecture can actually produce the desired graphic appearance. If not, redesign the relevant shading logic within the existing renderer.

### Desired appearance

The water should use a small number of coordinated, intentional visual layers:

1. **Base color regions:** broad areas of deep blue, cobalt, violet, turquoise, or other palette colors selected for the scene.
2. **Wave-face bands:** visible light and shadow shapes that describe the larger waves.
3. **Stylized crest accents:** curved, tapered marks following the shape and direction of the waves.
4. **Foam shapes:** selectively placed pale mint, warm white, or pink-white patterns rather than random white fragments.
5. **Sunlight reflection:** a coherent path of bright, broken horizontal marks connecting the sun to the foreground.
6. **Restrained fine detail:** smaller ripples that support the large forms without taking over the image.

These layers should feel like a single illustrated surface.

### Wave forms

The current surface has too many small, similar-looking ripples and shiny fragments competing for attention.

Create a hierarchy:
- A few large, readable wave forms.
- Medium-sized flowing bands that reinforce the wave direction.
- Sparse small accents for sparkle and motion.

Use directional, elongated patterns rather than uniform noise.

The shapes should flow along the wave contours and have deliberate variation in width, length, spacing, and curvature. Avoid evenly repeated stripes, regular procedural grids, and uniform horizontal oscillation.

The waves should remain attractive when viewed as a static image.

### Graphic shading

Use the wave geometry, surface orientation, depth where available, and carefully shaped procedural patterns to determine the color bands.

Prefer a designed set of color regions over simply posterizing the final lit RGB image. The result must be controllable and coherent across different lighting conditions.

Use anti-aliased transitions so the bands remain crisp without shimmering or producing jagged edges during animation.

Do not preserve physically realistic reflections simply because they already exist. Keep only the reflection behavior that supports the illustration.

### Foam

The existing white patches look like unrelated pieces of reflective noise rather than deliberately drawn water.

Replace that appearance with coherent, tapered foam marks that follow the wave structure.

- Use larger, more deliberate shapes.
- Break them into smaller fragments selectively.
- Align them with wave direction and crests.
- Keep their color and brightness consistent with the art direction.
- Avoid random islands of bright white scattered across the surface.

Foam should have an artistic purpose. It should not be present everywhere.

### Important implementation rule

Build and review the water in stages:

**Pass A:** Base colors and broad wave bands, without realistic reflections or fine foam.

**Pass B:** Add stylized crest patterns and a small number of graphic highlights.

**Pass C:** Add the sun's reflection as a distinct compositional element.

**Pass D:** Add restrained foam and small details.

Capture an image after each stage. Do not add detail until the previous stage looks good.

If Pass A does not already look like an illustrated ocean, stop and fix it before proceeding.

## 4. Give the sun a real relationship with the water

The current sun rays look dramatic in the sky but stop at the horizon. The sun feels like a separate background effect rather than the source of the scene's illumination.

Create a clear visual connection between the sun and the foreground ocean.

### Rays

- Preserve the attractive radial sunset effect, but refine its shape and intensity.
- Use broad, tapered rays with deliberate spacing and variation.
- Avoid making every ray equally bright or equally prominent.
- Let the rays blend naturally into the surrounding sky.
- Ensure the effect is stable and does not introduce distracting banding or aliasing.

### Sun reflection path

The sun should create a visible, stylized trail of light across the ocean.

Implement this as a coherent sequence of broken, elongated horizontal highlights that:
- Begins directly below the sun near the horizon.
- Widens and becomes more dispersed toward the foreground.
- Follows the perspective and orientation of the water.
- Uses warm peach, pale gold, pink-white, or other colors that suit the current palette.
- Is shaped by the wave forms rather than appearing as a straight, solid beam.
- Remains recognizable even when the water animation and palette change.

The reflection should be a major compositional feature, not just another collection of random specular glints.

Make the rays and water reflection read as two parts of the same lighting design.

Where appropriate, introduce a subtle warm illumination into the nearby water colors, but avoid uniformly tinting the entire ocean orange.

## 5. Replace the lumpy clouds with illustrated cloud shapes

Some current clouds resemble clusters of rounded blobs. Their individual forms are too spherical and irregular to read as convincing stylized clouds.

Redesign the cloud system around coherent silhouettes.

### Desired cloud language

Use horizontally stretched, layered cloud shapes with:
- Broad, connected masses.
- Clean, intentional outer contours.
- A limited number of lobes.
- Flatter undersides and softer or more varied upper contours.
- Warm illuminated edges near the sun.
- Cooler violet or mauve shadow regions.
- Deliberate variation in scale and spacing.

Clouds may overlap to establish depth, but they should read as connected cloud formations rather than piles of separate spheres.

Use a small number of large cloud shapes instead of many small, independent puffs.

### Avoid

- Dense clusters of identical rounded blobs.
- Excessively bumpy silhouettes.
- Obvious spheres or ellipsoids used as the final cloud appearance.
- Uniformly thick cloud bands stretching across the entire sky.
- Bright outlines around every individual lobe.
- Excessive procedural noise.

The goal is a sophisticated anime sunset sky with carefully composed cloud shapes.

### Animation

Use slow, coherent drift with subtle changes in cloud formation where supported by the existing system.

Do not animate every cloud puff independently. Clouds should feel like parts of larger atmospheric formations.

Keep the sun visible and protect the negative space around it. Cloud placement should enhance the sun's rays rather than obstructing them with a mass of bright shapes.

## 6. Preserve and improve the successful boats and dolphins

The boats and dolphins are promising. Do not discard them during the redesign.

Instead:
- Preserve the successful boat designs and improve their placement if necessary.
- Retain the dolphins' recognizable silhouettes and appealing motion.
- Ensure both integrate with the revised water palette and graphic shading.
- Avoid adding unnecessary boat or dolphin detail.
- Keep their scale and positioning coherent with the island and horizon.

Boats should have credible hulls and recognizable construction. Dolphins should remain elegant, readable forms rather than becoming highly detailed or overly cartoonish.

Their highlights, shadows, and water interactions must use the same illustrated visual language as the rest of the scene.

## 7. Unify the art direction

The current image has several attractive colors, but the scene does not yet feel like one coherent illustration.

Establish a small set of coordinated palettes that can transition smoothly with the music.

For the current sunset direction, explore:
- Deep violet and indigo shadows.
- Muted mauve and rose in the sky.
- Warm coral, peach, and pale gold around the sun.
- Deep cobalt or violet-blue water.
- Selective turquoise or pink-white highlights.

Do not independently randomize the colors of individual elements. The island, clouds, water, boats, and dolphins must belong to the same color design.

Use broad color regions, selective highlights, and controlled shading. Remove visual noise where it does not strengthen the composition.

The result should feel illustrated even when the scene is paused.

## 8. Reuse assets intelligently

Inspect the project's existing asset inventory before creating anything new.

In particular:
- Find suitable rocks from the existing rock assets.
- Find suitable trees or land components in the Tree of Life floating-island scene.
- Reuse successful boat and dolphin assets from Digital Ocean.
- Reuse existing scene, lighting, shader, and animation infrastructure where appropriate.

Avoid duplicating assets or building new generic procedural systems when existing project resources can provide better results.

If an existing asset has the wrong visual style, adapt it rather than using it unchanged.

## 9. Required visual review

Create a small contact sheet showing the following decisions:

1. Three island/camera compositions.
2. Three water treatments: broad color bands, bands plus graphic highlights, and bands plus highlights and restrained foam.
3. Two cloud treatments: simplified layered silhouettes and a slightly richer illustrated formation.
4. The selected combined scene.

Use the same resolution and comparable scene conditions so the differences are easy to evaluate.

Review both a still frame and a short animated capture. A still frame should establish the art direction; animation should demonstrate that the forms remain coherent in motion.

Do not select the most detailed variant by default. Select the one with the strongest composition, clearest silhouettes, and most intentional color design.

## 10. Performance and engineering constraints

- Maintain the existing AV Gen C++23, WebGPU/Dawn, and Metal rendering architecture.
- Do not introduce a new rendering backend or a general-purpose scene framework.
- Avoid unnecessary render passes and expensive full-screen effects.
- Keep the shader suitable for live audiovisual performance.
- Ensure the water bands, foam, cloud shapes, and rays do not shimmer excessively during animation.
- Profile the final result at the intended live resolution and at 1080p.
- Reuse the existing audio and MIDI signal infrastructure rather than analyzing audio inside the shader.

Audio reactivity should modulate a few coordinated properties such as wave amplitude, band contrast, crest intensity, foam density, and palette transitions. It must not destroy the readability of the composition.

## Acceptance criteria

- [ ] The scene reads as an illustrated anime environment rather than conventional CG with a purple color grade.
- [ ] The water's broad color bands and graphic wave patterns are immediately visible.
- [ ] Fine reflections no longer dominate the water's appearance.
- [ ] The sun creates a continuous visual relationship between sky rays and ocean reflection.
- [ ] Clouds have coherent, intentional silhouettes rather than lumpy procedural forms.
- [ ] The island is clearly visible and has a distinctive tree and a few well-placed rocks.
- [ ] Existing rock and Tree of Life assets have been investigated and reused where appropriate.
- [ ] Boats and dolphins remain attractive and integrated with the revised art direction.
- [ ] The composition works as a still image, not only as an animation.
- [ ] The water, sky, island, and objects share a coherent palette and shading language.
- [ ] The agent supplies comparison screenshots, an animated capture, and a performance report.

## Final creative directive

**Stop trying to make a conventional 3D ocean look more colorful. Design an illustrated ocean from the ground up.**

The island should be a memorable little silhouette. The water should be composed of flowing graphic shapes. The sun should paint a trail of light across the sea. The clouds should have elegant silhouettes.

Every element should contribute to the same illustration.
