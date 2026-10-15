# AV Gen — Digital Ocean Art Pass 2
## Illustrated Ocean World, Expanded Composition & Musical Color

*The owner's brief, verbatim (2026-10-15).*

### Mission

Perform a substantial visual art-direction pass on the existing Digital Ocean scene.

The current scene—primarily ocean water, a horizon, and a sun—is too sparse and visually uninteresting. Expand it into a richly composed, colorful, illustrated ocean environment with stronger environmental storytelling, more prominent foreground and background elements, expressive skies, and much more dramatic audio/MIDI-driven visual variation.

This is an evolution of the existing scene, not a new standalone scene. Preserve the working ocean animation and audio/MIDI integration where useful, but do not feel obligated to preserve visual decisions that are holding the scene back.

**Primary goal:** make Digital Ocean feel like an animated illustration that comes alive with music.

The result should be beautiful in a still frame, captivating in motion, and expressive during a live performance.

---

## 1. Establish a new art direction

Before implementing additional objects, revisit the visual language of the entire scene.

Explore a hand-illustrated aesthetic informed by one or more of these directions:

- **Japanese anime backgrounds:** painted skies, dramatic cloud formations, luminous sunsets, stylized ocean surfaces, and richly composed landscapes.
- **Japanese woodblock prints:** bold silhouettes, graphic wave shapes, simplified perspective, deliberate outlines, and striking color blocking.
- **Colorful editorial illustration:** expressive shapes, confident palettes, layered scenery, and stylized environmental detail.
- **Contemporary illustrated fantasy:** luminous atmospheric effects, surreal marine life, exaggerated scale, and richly colored skies.
- **Graphic travel-poster art:** simplified landscapes, oversized suns, layered islands, bold color fields, and elegant compositions.

Research visual references and create several palette and composition studies before settling on a direction. The references should guide the visual language without directly copying any particular artist or existing copyrighted artwork.

These directions can be combined. For example, painted anime-style clouds, graphic woodblock-inspired wave crests, and colorful illustrated island silhouettes could coexist beautifully.

### The crucial distinction

Do not simply apply outlines, cel shading, or a toon shader to the existing CG scene and consider the job done.

The entire image should be art-directed toward illustration:

- Strong, recognizable silhouettes.
- Intentional shape simplification.
- Deliberate color grouping.
- Broad, readable shading regions.
- Carefully layered scenery.
- Controlled detail and texture.
- Expressive, painterly atmospheric effects.
- A coherent relationship between foreground, middle distance, and background.

Where possible, favor custom-designed shapes and procedural geometry over generic stock-looking 3D assets.

The goal is not photorealism. It is a sophisticated, animated illustration with a distinctive visual identity.

## 2. Recompose the scene

The current flat horizon and uninterrupted water surface do not provide enough visual interest.

Create a stronger sense of place, scale, and depth.

### Suggested composition

Build the environment from several overlapping layers:

1. **Foreground:** larger, more expressive waves, foam accents, and potentially a partial shoreline or rocky silhouette.
2. **Middle distance:** a prominent boat, a larger dolphin, or other recognizable focal element.
3. **Island layer:** an island or archipelago with distinctive terrain and vegetation.
4. **Ocean distance:** layered wave bands with gradually simplified shapes and softer colors.
5. **Sky:** dynamic cloud formations, sun rays, and atmospheric color gradients.

These are compositional guidelines, not a requirement to fill every region with objects.

Preserve enough negative space that the scene can breathe. The aim is a rich illustration, not a cluttered collage.

Experiment with the camera angle and relative scale of the elements. The island should have enough visual mass to establish a location, while the water should remain the dominant animated surface.

A slightly more elevated, illustrative viewpoint may help reveal the wave patterns, island silhouette, and boat placement. Compare alternatives using actual screenshots.

## 3. Animated sun rays

The sun should become a major visual feature rather than a static disc on the horizon.

Develop stylized sun rays that can animate continuously and respond to music.

Possible treatments include:

- Long, tapered rays radiating from behind the sun.
- Broad, translucent triangular beams.
- Alternating warm and cool ray segments.
- Painterly radial bands.
- A subtle halo with layered concentric shapes.
- Light beams partially obscured by passing clouds.
- A stylized reflection connecting the sun visually to the ocean.

The effect should resemble an illustrated sky rather than a volumetric lighting demo.

### Animation

Use a combination of slow continuous movement and music-driven variation.

Potential controls:
- Ray length.
- Ray spread.
- Ray rotation or angular drift.
- Ray opacity.
- Halo size.
- Sun color.
- Warmth of the surrounding sky.

Audio-driven accents may briefly extend the rays, brighten the halo, or shift the surrounding colors on a strong musical transient.

Avoid rapid spinning, excessive strobing, and uniform pulsing of every ray. Use restrained animation and varied timing.

If clouds pass in front of the sun, consider masking or layering rays so they appear integrated with the sky.

## 4. Introduce an island or archipelago

Create at least one visually compelling island that establishes a sense of place.

It should be a recognizable environmental feature, not a generic green mound.

Explore silhouettes such as:
- A lush tropical island with exaggerated palm trees.
- A dramatic island with steep cliffs and layered rock formations.
- A distant chain of islands fading into atmospheric haze.
- A fantastical island with unusual vegetation and colorful terrain.
- A Japanese-inspired coastal landscape with distinctive tree silhouettes and layered hills.

The agent should choose a direction consistent with the new illustration style.

### Island construction

Use simple geometry, layered shapes, or custom procedural meshes. Prioritize the silhouette and color composition over geometric complexity.

Consider:
- Multiple overlapping terrain layers.
- Distinct rock and vegetation color regions.
- Stylized beaches or coastal highlights.
- A few carefully placed trees.
- Small distant structures, if they strengthen the setting.
- Atmospheric color shifts across distant landforms.

The island must remain visually legible at the intended output resolution.

### Trees and vegetation

Use custom silhouettes and deliberately varied forms.

Depending on the selected direction, these might include:
- Graphic palm trees.
- Layered pine silhouettes.
- Rounded, stylized tropical foliage.
- Windswept coastal trees.
- Exaggerated, colorful fantasy vegetation.

Animate them subtly through wind-driven movement. Trees should respond to the environment without distracting from the water.

**Acceptance criterion:** the island and vegetation should make the environment feel like a designed illustrated world, not a flat ocean with a few props added.

## 5. Boats as major visual elements

Introduce boats, giving them a much stronger visual presence than the current dolphins.

The boats should be large enough to read clearly and contribute to the composition.

Explore several concepts:
- A small, colorful sailing boat with an exaggerated sail.
- A traditional fishing boat with a graphic silhouette.
- A stylized Japanese-inspired fishing vessel.
- A surreal sailboat with an unusually large or colorful sail.
- A small fleet of boats distributed across different depth layers.

Choose a limited number. One beautifully designed hero boat may be more effective than many tiny boats.

### Design requirements

- Distinctive silhouette.
- Clear separation between hull, sail, and water.
- A palette that contrasts with the ocean.
- Simple, clean geometry appropriate to the illustration style.
- Visible but restrained bobbing and rocking.
- A wake or small trail that follows the boat's motion.
- Consistent scale relative to waves and the island.

Use wind direction to inform sail orientation and motion where practical.

Boat motion should feel connected to the water. Avoid boats that hover, move rigidly through waves, or rock independently of their surroundings.

### Audio/MIDI interaction

Consider mapping scene energy to:
- Boat rocking amplitude.
- Sail movement.
- Wake intensity.
- Boat speed or path variation.
- Brief changes in sail or hull accent colors.

MIDI-triggered events could send a boat across the frame or introduce a larger wave that the boat rides over.

These interactions should feel like intentional visual gestures rather than unrelated animation triggers.

## 6. Make the dolphins substantially larger

The existing dolphins are too small to read clearly.

Increase their size significantly and evaluate their silhouette against the sky, water, and island. Do not merely scale them up without revisiting their animation and placement.

A dolphin leap should be an identifiable visual event.

### Animation

Develop a complete animated gesture:

1. A subtle approach or preparation.
2. A curved ascent from the water.
3. A clearly visible airborne arc.
4. A brief apex with a readable silhouette.
5. A descent and re-entry.
6. A stylized splash, foam burst, or wake.

The entire event should feel graceful and deliberate.

Use a contrasting color or light accent along the body where appropriate, but retain the illustrated visual language.

Consider placing a dolphin in the middle distance where it is large enough to recognize without competing with the boat or island.

The dolphin should not appear continuously. Give it moments of activity separated by quieter periods.

Audio or MIDI can influence the likelihood, timing, or scale of a leap. Avoid spawning multiple overlapping dolphins on every beat.

## 7. Dynamic illustrated clouds

Clouds should become one of the primary sources of visual motion.

Replace any sparse, static, or generic clouds with large, expressive formations inspired by painted anime skies or graphic illustration.

### Cloud design

Explore:
- Broad, layered cloud banks.
- Puffy, stylized cumulus silhouettes.
- Long, wind-stretched cloud ribbons.
- Warmly illuminated cloud edges.
- Deep violet or blue shadow regions.
- Thin cloud layers passing in front of the sun.
- Distant clouds that simplify into flat shapes.

Vary cloud scale, density, spacing, and silhouette. Avoid rows of identical procedural puffs.

Clouds can be built from layered meshes, custom silhouettes, billboards, or other efficient techniques supported by AV Gen.

### Animation

Combine:
- Slow horizontal drift.
- Subtle changes in cloud shape.
- Layered parallax.
- Gradual changes in cloud color.
- Music-driven illumination or color accents.

Cloud motion should be continuous and atmospheric, not jittery or excessively fast.

Consider using a dominant cloud formation that slowly changes the balance of the composition as it passes the sun.

Clouds can also create visual contrast during musical transitions: a warm, open sky might gradually become deep blue and violet before returning to a luminous sunset.

## 8. Make color modulation a first-class feature

This is one of the most important requirements of the art pass.

The scene currently lacks sufficient visual variation. Color, lighting, and wave intensity should evolve in musically meaningful ways.

Build a coherent scene-wide palette and modulation system using AV Gen's existing signal, parameter, and modulation architecture.

### Palette families

Develop at least three well-designed palettes that can transition smoothly.

Potential examples:

**Golden afternoon**
- Turquoise and teal water.
- Warm cream foam.
- Coral and orange sunlight.
- Soft blue sky.
- Warm green island accents.

**Violet sunset**
- Deep cobalt water.
- Magenta and lavender wave highlights.
- Peach sun rays.
- Pink-orange clouds.
- Dark violet island silhouettes.

**Electric blue**
- Ultramarine and cyan water.
- Pale mint surf.
- Saturated blue sky.
- Coral or yellow focal accents.
- Deep navy silhouettes.

The agent may develop different palettes if another illustration style proves more successful.

Every palette must be designed as a coordinated whole. Do not independently randomize the water, clouds, island, and sun colors.

### Continuous palette modulation

Implement smooth, controlled color transitions.

Potential influences:
- Overall audio energy.
- Low-frequency energy.
- High-frequency energy.
- Sustained harmonic content.
- Musical transients.
- MIDI controller values.
- MIDI-triggered palette changes.

Different influences should affect different visual properties.

For example:
- Bass energy may deepen the water and intensify wave highlights.
- High-frequency energy may introduce brighter aqua and mint accents.
- Sustained harmonic energy may gradually shift the sky toward violet or pink.
- Strong transients may produce a short-lived warm highlight.
- A MIDI controller may blend between two compatible palettes.

Use smoothing and deliberate time constants. Palette changes should feel like a living painting changing its lighting and atmosphere, not a rapidly cycling RGB effect.

### Suggested parameters

Expose a concise, useful set of controls through existing AV Gen mechanisms:

- `palette_blend`
- `water_color_intensity`
- `wave_amplitude`
- `wave_energy`
- `sun_ray_intensity`
- `cloud_color_intensity`
- `atmosphere_intensity`
- `scene_energy`

Adapt names to existing project conventions and reuse existing parameters wherever practical.

Do not expose dozens of low-level color channels when a few high-level artistic controls will work better.

## 9. Wave intensity and visual rhythm

The ocean must respond more expressively to the music.

Wave intensity should vary across the surface, not simply cause the entire ocean to move uniformly up and down.

Explore a combination of:
- Broad swell amplitude.
- Secondary wave height.
- Wave propagation speed.
- Crest sharpness.
- Foam intensity.
- Surface highlight intensity.
- Localized wave events.
- Different motion scales for foreground and distant water.

### Audio behavior

Potential mappings:
- Bass increases broad swell amplitude and the apparent weight of the ocean.
- Kick transients briefly intensify a nearby crest.
- Snare transients accentuate foam or create a brief splash.
- Sustained energy increases the richness of the wave colors.
- High-frequency energy adds delicate highlights to the water.
- Overall loudness controls the maximum visual intensity.

Use smoothed signals for continuous motion and event triggers for distinct accents.

Maintain a baseline level of wave movement when no music is playing.

### MIDI behavior

Allow a performer to:
- Raise or lower wave intensity.
- Increase wave speed.
- Shift the dominant wave direction.
- Trigger a larger wave event.
- Increase foam and spray.
- Blend between palettes.
- Trigger a dolphin leap or boat entrance where practical.

A MIDI control should have a perceptible and predictable effect across its range.

## 10. Animation and visual coherence

All environmental elements must feel like they inhabit the same world.

Establish a common sense of:
- Wind direction.
- Wave direction.
- Sunlight direction.
- Atmospheric perspective.
- Animation tempo.
- Palette relationships.

Boats should ride the water. Trees should react to wind. Clouds should drift in a consistent atmospheric direction. Sun rays should interact visually with clouds. Dolphins should emerge from and return to the water.

Not every element should respond to every musical beat. Give different layers their own timescales and behaviors.

Use slow ambient motion as the foundation, medium-speed music-driven changes as the main expressive layer, and occasional discrete events as highlights.

The scene should be engaging during a quiet musical passage without becoming visually exhausting during a loud one.

## 11. Required VFX-artist iteration process

Implement the art pass in visible, reviewable stages.

### Pass A — Visual research and style frames

Capture the current scene and create several actual in-engine or otherwise representative composition and palette studies.

Choose the strongest art direction before committing to detailed assets.

### Pass B — Composition and silhouette

Add the island, trees, boats, and larger dolphins using simplified forms.

Do not start with detailed materials. First establish a compelling silhouette and composition.

### Pass C — Illustration treatment

Refine water, clouds, land, and marine elements into a consistent illustrated style.

Evaluate whether the image genuinely looks illustrated rather than merely cel-shaded.

### Pass D — Atmospheric effects

Add animated sun rays, cloud movement, atmospheric gradients, foam, and other supporting effects.

### Pass E — Musical color and wave modulation

Implement the major continuous controls and audio-reactive behaviors. Tune the relationships between different frequency bands and visual properties.

### Pass F — MIDI performance interactions

Integrate the main expressive parameters and a small set of signature events.

### Pass G — Visual critique

Run the scene and capture representative output at several points in a musical performance.

Inspect the actual frames. Identify the three most significant visual shortcomings and address them.

Repeat until the scene looks cohesive.

### Pass H — Performance and reliability

Measure the real live-rendering path at the intended resolution and at 1080p.

Check continuous playback, rapid MIDI triggering, repeated events, and transitions between palettes.

Document any remaining limitations.

Do not skip the rendered-output review or declare success solely because the scene builds.

## 12. Acceptance criteria

The art pass is complete when:

- The scene has a distinctive illustrated aesthetic.
- The composition is substantially richer than the current ocean, horizon, and sun.
- Animated sun rays are visually integrated with the sky.
- At least one substantial island and recognizable vegetation are present.
- Boats are large and visually readable enough to contribute to the composition.
- Dolphins are significantly larger and their leaps read clearly.
- Clouds have varied silhouettes and meaningful continuous motion.
- The palette can transition smoothly between several coordinated color schemes.
- Water color and wave intensity respond expressively to music.
- MIDI controls can meaningfully alter the scene.
- Quiet passages remain visually engaging.
- Loud passages become more energetic without devolving into visual noise.
- All major elements share a consistent illustration style.
- Actual rendered output has been reviewed and improved iteratively.
- Performance and event handling have been validated.

## Final creative direction

Think of this as an illustrated ocean world that could appear in a beautifully animated music video or an experimental Japanese-inspired visual artwork.

Make the island, boats, dolphins, clouds, sun rays, and waves large enough to establish an unmistakable visual identity. Let color and wave motion become the primary musical instruments. Use the new elements to create depth, atmosphere, and memorable moments rather than simply filling empty space.

Take creative risks, compare alternatives, and follow the visual direction that produces the most compelling result.

**The target is a vibrant, hand-art-directed ocean illustration that continuously transforms with the music—not a conventional 3D ocean scene with more objects added.**
