# Cymatic World — Multi-Palette Color Modulation

(Owner brief, received 2026-10-16, saved verbatim — the full spec, replacing an earlier truncated paste. The owner's preface: "I don't think it understood what I meant by modulating color, I want a variety of color palettes, here are some more it can work with — we want shape shifting and color palette shifting in this scene.")

## Objective

Upgrade Cymatic World so it can move through a curated variety of visually distinct color palettes over time, driven by the music and existing resonance system.

**This is not a request for a hue-rotation effect, rainbow shader, or one fixed palette with slight color variation.** I want the scene to transform between distinct, intentional color worlds while preserving the coherent cymatic geometry that is already working well.

The geometry should remain the primary visual attraction. Color, material response, lighting, and highlights should reinforce the geometry's resonant behavior rather than obscure it.

Before implementation, inspect the existing color, material, lighting, post-processing, and audio-modulation systems. Reuse the established architecture wherever possible. Do not build a competing resonance or modulation system.

## 1. Implement these 10 artist-designed palettes

Treat these as actual palette definitions with named color roles, not merely lists of colors to cycle through uniformly.

### Palette A — Abyssal Resonance
Deep, mysterious, luminous, and expansive.

- Background: `#050711`
- Deep shadow: `#10152B`
- Primary surface: `#202D59`
- Secondary surface: `#354C88`
- Resonant blue: `#527FE0`
- Spectral highlight: `#A3C8FF`
- Rare peak accent: `#E5EDFF`

Character: enormous dark forms with cool spectral highlights. Emphasize depth, folds, and the sensation of energy traveling through a submerged structure.

### Palette B — Crimson Singularity
Dark indigo-violet contrasted with concentrated crimson energy.

- Background: `#080612`
- Deep shadow: `#180D29`
- Primary surface: `#301448`
- Secondary surface: `#54205F`
- Resonant magenta: `#A52361`
- Spectral red: `#F04455`
- Rare peak accent: `#FF9AA5`

Character: dark mathematical sculpture with red energy concentrated along resonant ridges and interference regions. Dramatic, intense, and high contrast. Red should not cover every surface.

### Palette C — Molten Glass
Blackened surfaces with glowing amber, orange, and gold.

- Background: `#0B0808`
- Deep shadow: `#26110F`
- Primary surface: `#51201A`
- Secondary surface: `#87351F`
- Resonant orange: `#D9572B`
- Spectral gold: `#FFAD45`
- Rare peak accent: `#FFE3A1`

Character: heated material, molten glass, and energy under pressure. Keep the dark regions rich and the hottest colors concentrated in selected folds, ridges, and interference patterns. Avoid making the whole scene look like lava.

### Palette D — Ultraviolet Bloom
Violet, electric purple, fuchsia, and pale lavender.

- Background: `#080611`
- Deep shadow: `#1A1033`
- Primary surface: `#30205A`
- Secondary surface: `#58318A`
- Resonant purple: `#8A45C6`
- Spectral fuchsia: `#D45AAE`
- Rare peak accent: `#F4C7FF`

Character: a luminous, surreal, almost blacklight-like sculpture. Use violet as the foundation and fuchsia as a selective response to musical energy.

### Palette E — Oxidized Metal
Aged copper, dark bronze, and pale metallic gold.

- Background: `#090B0D`
- Deep shadow: `#1B2022`
- Primary surface: `#39413F`
- Secondary surface: `#665044`
- Resonant copper: `#AD6847`
- Spectral bronze-gold: `#D8A76C`
- Rare peak accent: `#F5DDB0`

Character: tactile, sculptural, and metallic. Use lighting and grazing highlights to suggest reflective metal without requiring expensive real-time reflections. Avoid a uniform brown or bronze wash.

### Palette F — Glacial Spectral
Cold white, ice blue, steel blue, and deep navy.

- Background: `#050A12`
- Deep shadow: `#101B2A`
- Primary surface: `#1D344C`
- Secondary surface: `#315B79`
- Resonant ice blue: `#65A9CE`
- Spectral silver-blue: `#B7DCEB`
- Rare peak accent: `#F1FCFF`

Character: crystalline, cold, and spacious. Preserve substantial dark negative space. The brightest highlights should feel like light catching the edges of a complex sculptural form, not a glowing blue wireframe.

### Palette G — Chlorophyll After Dark
Deep forest, emerald, and restrained acidic yellow-green.

- Background: `#050A08`
- Deep shadow: `#102017`
- Primary surface: `#193D2A`
- Secondary surface: `#286746`
- Resonant green: `#39A66A`
- Spectral chartreuse: `#A4D65E`
- Rare peak accent: `#E6F5A4`

Character: organic, alien, and bioluminescent. Avoid the generic bright-green sci-fi aesthetic. Green should emerge from darkness, with yellow-green reserved for selected resonant peaks.

### Palette H — Rose Quartz
Smoky plum, dusty rose, muted pink, and pearl.

- Background: `#0A0710`
- Deep shadow: `#211522`
- Primary surface: `#47263F`
- Secondary surface: `#75405E`
- Resonant rose: `#B65D83`
- Spectral pink: `#EAA0BA`
- Rare peak accent: `#FFE0EA`

Character: softer and more delicate without losing sculptural depth. Think translucent mineral formations and light scattering through layered surfaces, not a flat pink filter.

### Palette I — Monochrome Silver
Black, charcoal, steel, silver, and white.

- Background: `#030405`
- Deep shadow: `#101215`
- Primary surface: `#282C32`
- Secondary surface: `#464D57`
- Resonant silver: `#777F8C`
- Spectral silver-white: `#C2C9D2`
- Rare peak accent: `#FFFFFF`

Character: pure form, contrast, and light. This palette should prove that the geometry and lighting are compelling without relying on saturated color. Musical intensity can control the transition from charcoal to silver-white.

### Palette J — Prismatic Oil
Deep petrol, violet, burgundy, and shifting spectral reflections.

- Background: `#05070D`
- Deep shadow: `#101426`
- Primary surface: `#20203F`
- Secondary surface: `#442447`
- Resonant burgundy: `#813450`
- Spectral rose-gold: `#D77C88`
- Rare peak accent: `#F4D4C5`

Character: an iridescent, oil-slick-like material with complex violet-to-burgundy reflections. This should feel materially different from the other palettes, not like a standard purple scene with pink highlights.

Use the existing material and rendering capabilities where possible. If convincing iridescence is too expensive or technically unsupported, approximate it with view-dependent color variation and restrained lighting rather than introducing a costly new rendering path.

## 2. Give every palette a coherent color hierarchy

Each palette has three kinds of color:

1. **Foundation:** background, shadows, and dark surface regions. These establish depth and should remain dark enough to preserve contrast.
2. **Resonance colors:** the dominant midtones and saturated colors that reveal the geometry's changing shapes and modes.
3. **Spectral highlights:** rare bright accents that appear on selected ridges, creases, interference regions, or areas of high resonant energy.

Do not distribute every color equally across the entire scene. Avoid uniformly coloring all vertices, surfaces, or particles.

Color should help reveal the structure of the cymatic geometry. Major folds and coherent modes should have broad color relationships; fine detail can have sharper accents.

The brightest color should not automatically cover the entire scene when the music gets louder.

## 3. Make palette changes musically meaningful

Palettes must transition smoothly, but they should not all blend continuously or randomly every frame.

Implement a palette state system using the existing modulation architecture:

- A current palette and a target palette.
- A normalized transition parameter.
- Smooth interpolation between corresponding palette roles.
- A configurable transition duration and response.
- A mechanism for choosing the next palette in response to musical phrases, energy changes, or explicit user control.
- A manual way to select or lock a palette for performances.

Interpolate in a perceptually appropriate color space, such as OKLab, if practical. Avoid muddy intermediate colors, sudden brightness changes, or unintended flashes during transitions.

**Do not use unrestricted random color selection.** Palette changes should feel composed and intentional.

Musical behavior should work approximately as follows:

- **Bass:** modulates the intensity and extent of broad color regions.
- **Low mids:** influence large-scale shifts in the dominant surface colors.
- **Mids:** reveal resonant bands, folds, and interference patterns.
- **High frequencies:** produce fine spectral highlights and subtle changes in surface response.
- **Transients:** trigger brief, localized highlight events or traveling accents rather than flashing the entire scene.
- **Sustained tones:** allow coherent color patterns to stabilize and resonate.
- **Musical phrase changes:** provide opportunities to transition to another palette.
- **Silence or low energy:** retain a beautiful, slowly evolving visual state rather than turning black or freezing.

These are intended relationships, not a mandate to map every audio band directly to a color channel. Reuse the existing resonance and modulation mechanisms and tune the mappings for visual quality.

## 4. Let the geometry and lighting participate in the color

The goal is not merely to recolor the existing scene.

Explore coordinated changes to:

- Surface base color and material response.
- Grazing highlights that reveal folds and ridges.
- Restrained emissive accents along high-energy regions.
- The relationship between dark valleys and illuminated peaks.
- Specular response appropriate to each palette's material character.
- Subtle bloom or other existing post-processing, where it improves the image.

Different palettes may suggest different material qualities, but they must still read as the same Cymatic World scene.

Do not make bloom, emission, or global brightness the primary source of visual interest. Preserve geometry, shading, and negative space.

## 5. Build a palette comparison mode

Before finalizing the effect, create a way to inspect all 10 palettes against the same representative geometry and lighting conditions.

Capture screenshots or rendered comparisons that let us evaluate:

- Each palette independently.
- A transition between two visually contrasting palettes.
- A quiet musical passage.
- A dense, high-energy passage.
- A strong transient.
- A sustained resonant tone.

Keep camera position, geometry, and exposure consistent for the basic palette comparison. Otherwise it will be difficult to judge whether the palette itself is working.

Then evaluate the palettes in motion. A palette that looks good in a still image may look muddy, washed out, or excessively noisy when the geometry moves.

## 6. Performance and integration

This is a live audiovisual instrument.

- Reuse the existing rendering and resonance infrastructure.
- Avoid CPU-side per-vertex color recalculation if the current renderer can perform the work on the GPU.
- Avoid unnecessary allocations during palette transitions.
- Avoid introducing expensive full-screen effects just to make the colors feel responsive.
- Profile the scene with palette modulation enabled and disabled.
- Preserve stable geometry and readable color transitions under sustained high-energy input.
- Expose useful controls through the existing scene-control UI and modulation system.

Palette selection and transition should be lightweight compared with the geometry and rendering work already being performed.

## 7. Acceptance criteria

The work is not complete merely because ten sets of hex values exist in code.

It is complete when:

1. All 10 palettes produce visibly distinct and compelling results.
2. Palette transitions are smooth and do not produce muddy colors or brightness jumps.
3. The scene can move between palettes during a performance without interrupting the geometry.
4. Color modulation responds to the music in a coordinated way rather than behaving like random hue animation.
5. The dark foundation, resonant midtones, and selective highlights remain legible in every palette.
6. The scene retains its visual identity across all palettes.
7. The result is compelling in motion, not only in screenshots.
8. Performance remains appropriate for live use.

Deliver the implementation, the palette definitions, the relevant controls, comparison screenshots, a brief explanation of the transition logic, and before/after performance measurements.

Prioritize artistic quality over simply completing the list. If one palette does not work well with the actual geometry, adjust its color roles while preserving its intended identity.

The intended result is a single coherent cymatic instrument capable of moving between multiple distinct visual moods as the music unfolds.
