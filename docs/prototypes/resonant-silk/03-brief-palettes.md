# Resonant Silk — Dedicated Palette System and Musical Color Modulation

(Owner brief, received 2026-10-16, saved verbatim. The owner's preface: "Resonant [Silk] should also be very colorful, here are some more palettes for that agent too." This supersedes the single palette in 00-brief.md §4.)

## Objective

Give Resonant Silk its own curated collection of distinct, artist-designed color palettes and the ability to transition between them musically.

This system belongs specifically to Resonant Silk. Do not share Cymatic World's palette definitions or simply reuse its palette collection. Shared low-level color interpolation utilities are fine, but the palettes, color roles, material tuning, and artistic behavior must be authored for this scene.

Resonant Silk is a flowing, sculptural silk-like surface with continuous folds, overlapping layers, traveling waves, and grazing highlights. Its colors should make those surfaces feel tactile, luminous, and dimensional.

**The goal is to make the same silk sculpture feel like different extraordinary materials as the music evolves—not to cycle a rainbow over a generic shader.**

## 1. Implement these 8 dedicated palettes

Each palette defines the overall surface character, not just its hue. Use these as starting values and tune them against the actual rendered geometry.

### A. Midnight Aurora

Dark indigo silk with cool violet and icy lavender highlights.

- Environment: `#070711`
- Shadow: `#15152C`
- Base silk: `#29264F`
- Fold color: `#51477F`
- Resonant highlight: `#8F87D5`
- Grazing highlight: `#C5C4F3`
- Peak sheen: `#F0EDFF`

Character: expansive, elegant, and mysterious. Large dark folds with luminous lavender light sliding over their edges.

### B. Garnet Veil

Blackened plum, wine-red, crimson, and rose.

- Environment: `#09050C`
- Shadow: `#1C0D20`
- Base silk: `#3B1735`
- Fold color: `#712447`
- Resonant highlight: `#B83A60`
- Grazing highlight: `#F06B83`
- Peak sheen: `#FFC0CB`

Character: luxurious and dramatic. Red should collect along selected folds and ridges while the deeper plum surfaces retain their darkness.

### C. Liquid Champagne

Warm ivory, muted gold, bronze, and champagne highlights.

- Environment: `#0C0908`
- Shadow: `#261C17`
- Base silk: `#57402F`
- Fold color: `#8A6544`
- Resonant highlight: `#C69B65`
- Grazing highlight: `#F0D5A0`
- Peak sheen: `#FFF4D8`

Character: flowing metallic fabric, warm and refined. Highlights should resemble soft reflected light on fine silk rather than polished gold metal.

### D. Opal Dream

Pearlescent blush, lavender, warm white, and subtle rose.

- Environment: `#0B0710`
- Shadow: `#241827`
- Base silk: `#51364F`
- Fold color: `#845B79`
- Resonant highlight: `#C58AA8`
- Grazing highlight: `#E9C4D8`
- Peak sheen: `#FFF0F3`

Character: delicate, layered, and almost translucent in appearance. Use restrained color shifts across viewing angles to suggest opalescence. Do not wash the entire scene in pale pink.

### E. Deep Emerald Satin

Blackened green, rich emerald, and pale mint highlights.

- Environment: `#050A08`
- Shadow: `#10221A`
- Base silk: `#1C4535`
- Fold color: `#307456`
- Resonant highlight: `#4BA783`
- Grazing highlight: `#91D6B0`
- Peak sheen: `#DFFFEA`

Character: dark, rich, organic luxury. The silk should feel deep and tactile, not like a fluorescent green sci-fi surface.

### F. Electric Cobalt

Saturated cobalt, royal blue, and icy blue-white highlights.

- Environment: `#050711`
- Shadow: `#10162D`
- Base silk: `#1B3470`
- Fold color: `#2856A0`
- Resonant highlight: `#478CE0`
- Grazing highlight: `#8DC7FF`
- Peak sheen: `#E2F3FF`

Character: energetic, crisp, and dimensional. Preserve deep blue shadow regions while allowing moving folds to catch bright, cool highlights.

### G. Burnished Copper

Dark umber, burnt sienna, copper, and pale peach.

- Environment: `#0B0707`
- Shadow: `#261411`
- Base silk: `#51251D`
- Fold color: `#843C2B`
- Resonant highlight: `#C56343`
- Grazing highlight: `#F0A071`
- Peak sheen: `#FFD9BA`

Character: warm, sculptural, and tactile. Highlights should reveal the form of the fabric, not make it look like flowing lava.

### H. Oil-Slick Iridescence

Near-black violet, burgundy, indigo, and shifting rose-gold reflections.

- Environment: `#05050A`
- Shadow: `#121020`
- Base silk: `#25203C`
- Fold color: `#49304F`
- Resonant highlight: `#80405D`
- Grazing highlight: `#C46C83`
- Peak sheen: `#F2C4B6`

Character: dark, spectral, and materially complex. Use subtle view-dependent iridescence or a restrained approximation using existing shader capabilities. The underlying silk must remain legible; do not turn the whole scene into a rainbow oil slick.

## 2. Map color roles to the actual silk geometry

Do not assign colors uniformly or simply interpolate each vertex between two palette colors using audio amplitude.

Use the existing surface attributes, normals, lighting, and resonance signals to establish a coherent material response.

- **Base silk:** establishes the main material identity.
- **Fold color:** reveals valleys, overlapping surfaces, and changes in curvature.
- **Resonant highlight:** emphasizes selected folds and regions of elevated resonant energy.
- **Grazing highlight:** catches surfaces at appropriate viewing and lighting angles.
- **Peak sheen:** reserved for narrow, high-intensity highlights and exceptional musical moments.
- **Environment and shadow:** preserve contrast and give the sculpture a sense of depth.

The color distribution should follow the surface and its lighting, not look like an arbitrary painted gradient.

## 3. Give Resonant Silk its own transition behavior

Implement a dedicated palette selector and transition system for this scene.

- Smoothly interpolate between palette roles.
- Make transitions configurable in duration and responsiveness.
- Allow manual palette selection and a palette-lock mode for live performance.
- Support intentional automatic palette changes at musical phrase boundaries or significant energy changes.
- Avoid rapid random switching and uncontrolled hue rotation.
- Keep transitions visually stable when the audio signal fluctuates rapidly.
- Preserve dark areas and highlight contrast throughout each transition.

Use perceptually appropriate interpolation where practical, and test intermediate colors. A transition between green and red, for example, should not introduce large muddy brown regions across the entire fabric.

The scene should not need to change geometry or reset its resonance state when a palette changes.

## 4. Make the palette respond to silk-specific material qualities

Audio modulation should influence the appearance of the silk in ways that reinforce its physical character.

- **Bass:** increase the breadth and depth of broad fold shading.
- **Low mids:** strengthen color separation across major folds.
- **Mids:** reveal moving highlights and resonant color bands along the surface.
- **High frequencies:** introduce fine, restrained spectral sheen and narrow highlight details.
- **Transients:** send brief traveling highlight accents along the silk rather than flashing the entire surface.
- **Sustained tones:** allow coherent highlight patterns to settle into stable resonant modes.
- **Silence:** retain a slow, subtle movement of light and material response.

Reuse the existing resonance architecture. Do not implement another audio analyzer or a separate competing modulation system.

Audio intensity should not simply drive global saturation or emission. Preserve the material's identity at both low and high energy.

## 5. Palette-specific material tuning

Palettes may use slightly different material parameters where this materially improves the result:

- Highlight intensity and width.
- Specular roughness.
- Iridescence strength.
- Emission intensity.
- Fold contrast.

Keep these variations restrained. Every palette should still look like Resonant Silk, rather than eight unrelated rendering styles.

Prefer existing renderer capabilities. Do not introduce expensive rendering features without demonstrating that they materially improve the scene.

## 6. Validation

Capture comparisons for all eight palettes using the same camera, representative geometry, and comparable exposure.

Also demonstrate:

1. A transition between Midnight Aurora and Garnet Veil.
2. A transition between Deep Emerald Satin and Liquid Champagne.
3. Oil-Slick Iridescence in motion.
4. High-energy audio with traveling highlight accents.
5. Sustained tones with stable, coherent surface coloration.
6. Low-energy playback with subtle material movement.

Review the results in motion, not just as still images. Look for muddy transitions, blown-out highlights, flat color washes, excessive bloom, and loss of fold definition.

Deliver the eight palette definitions, scene-specific controls, screenshots, transition behavior, and performance measurements.

## Acceptance criteria

Resonant Silk must have a distinct palette system of its own. Its palettes should be recognizably different from one another, designed for flowing silk rather than generic geometry, and capable of transitioning musically without interrupting the scene.

Do not consider the feature complete merely because the palette selector works. The actual rendered fabric must look compelling in every palette.
