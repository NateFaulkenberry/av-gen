# AV Gen Scene: Waveform Dimension
## Detailed art direction, field-based geometry, resonance, shading, and implementation spec

(Owner brief, received 2026-10-16, saved verbatim. Coordinator notes: the owner has said to ignore the 120 Hz / 8.33 ms reference — the live target is 1080p at 30 fps; live scenes carry no audio file — test with assets/audio/feline-footwear.wav fed on the command line. Resonant Lattice was cut by the owner; Resonant Silk is in progress in a separate worktree.)

## Mission

Build a new production-quality AV Gen scene called **Waveform Dimension**.

The scene transforms a coherent, evolving three-dimensional scalar field into a sculptural environment of nested surfaces, flowing contour sheets, interference boundaries, and luminous spatial patterns.

Imagine standing inside a three-dimensional mathematical phenomenon: vast curved surfaces pass through one another, bands of color gather along resonant boundaries, and waves propagate through the sculpture. The field continuously evolves with the music, creating new spatial relationships while preserving a recognizable visual identity.

The scene should combine the visual qualities of:
- Elegant mathematical sculpture.
- Interference and standing-wave patterns.
- Layered translucent or semi-opaque membranes.
- Topological transitions and nested contours.
- Carefully art-directed gradients and grazing illumination.
- Slow, immersive camera movement through an enormous spatial object.

This is an audiovisual artwork, not a conventional waveform visualizer or scientific data viewer.

The initial target is a compelling live scene, not a computationally exhaustive field simulation. Reuse AV Gen's existing resonance, audio-analysis, modulation, scene, and rendering infrastructure wherever possible.

## 1. First task: audit existing resonance infrastructure

Before implementing a new field or resonance system, inspect the existing AV Gen architecture and Cymatic World.

Trace the existing signal path from audio analysis through the signal bus, timeline, modulators, parameters, generators, scene, and renderer. Identify whether the current resonance implementation provides persistent modes, stable phases, damping, transient excitation, spatial field evaluation, and deterministic behavior.

Document:
- Which resonance facilities already exist.
- Which are generic engine features and which are specific to Cymatic World.
- How a scene can access audio bands, onset/transient signals, and smoothed envelopes.
- Whether the renderer already supports suitable GPU field evaluation, mesh generation, shader deformation, or post-processing.
- What can be reused directly and what concrete capability is missing.

Reuse the existing system wherever possible. Extend it only where a demonstrated gap prevents the desired behavior. Do not create a parallel audio-analysis pipeline or a competing global resonance framework.

If another art agent is already working on Resonant Silk or Resonant Lattice, coordinate through a short technical note rather than editing the same scene files or shared engine components concurrently.

## 2. Art direction

### The central image

The scene consists of several families of continuous, curved surfaces derived from one underlying spatial field.

These surfaces should appear to weave through the scene, creating nested forms, openings, enclosed pockets, folds, and interference boundaries. Some areas form large, quiet expanses; others compress into tight, intricate bands.

The overall image should have a clear hierarchy:

1. **Primary form:** a small number of large, elegant surfaces that define the scene's silhouette.
2. **Secondary structure:** nested contours and neighboring sheets that create depth and reveal the field's internal organization.
3. **Fine detail:** narrow interference bands, subtle ridges, and small-scale illumination that become visible during stronger musical passages.

The geometry should not simply consist of evenly spaced parallel planes. The surfaces should curve in multiple directions and interact spatially.

### Desired aesthetic

Think of an abstract installation made from luminous, dark, iridescent material. It is simultaneously precise and mysterious: an underlying mathematical order is apparent, but the entire form is not immediately understandable.

Use:
- Sweeping curvature.
- Uneven spacing between contour layers.
- Large openings and negative space.
- A few regions of dense interference.
- Strong near/far depth cues.
- Selective translucency or layered shading.
- Subtle surface deformation that exposes wave propagation.

Avoid:
- Generic terrain or topographic contour maps.
- A stack of flat sine-wave sheets.
- Equally spaced, uniformly illuminated layers.
- An ordinary oscilloscope rendered in 3D.
- Dense polygon wireframes.
- Rainbow gradients covering every surface.
- Excessive particles used to fill empty space.
- Randomly changing geometry with no persistent structure.

The scene should remain recognizable from its silhouette and large-scale structure even when color and lighting are temporarily disabled.

## 3. Color palette

Start with a dark, restrained, iridescent palette. The following hex values are visual references in sRGB; convert correctly for use in linear-light rendering.

- **Deep void:** #070918
- **Midnight blue:** #111B43
- **Indigo:** #363A91
- **Electric cyan:** #38DCE8
- **Glacial teal:** #52D7C3
- **Ultraviolet:** #8558F4
- **Magenta accent:** #E94CCB
- **Pale spectral highlight:** #DDFBFF

Use dark blue and indigo to establish the large forms. Cyan and teal should reveal primary contours and areas of active resonance. Violet should emerge where different modes interact. Magenta should appear selectively, particularly at strong interference regions or during carefully controlled peaks. Pale highlights should be rare.

The colors must follow the geometry and the field structure. Do not cycle the entire scene through the spectrum.

The palette should communicate both depth and mathematical organization. Two neighboring surfaces may have related hues but different illumination, helping the viewer distinguish them without making them look like disconnected objects.

## 4. Field representation

The scene should be driven by a coherent three-dimensional scalar field:

\[
F(\mathbf{x},t)=\sum_{i=1}^{N} A_i(t)\,
\phi_i(\mathbf{x},t)
\]

Where:
- \(\mathbf{x}\) is a three-dimensional position.
- \(t\) is time.
- \(A_i(t)\) is the evolving amplitude of mode \(i\).
- \(\phi_i\) is a spatial mode with a persistent identity and phase.

This is a conceptual starting point, not a mandate to use a particular mathematical basis. The implementation should reuse the engine's existing resonance representation if suitable.

Possible spatial modes include:
- Broad radial or ellipsoidal patterns.
- Curved wavefronts.
- Angular or lobed patterns.
- Intersecting wave families.
- Traveling waves.
- Standing-wave patterns with stable nodes and antinodes.
- Smooth asymmetric modes that prevent the scene from becoming too geometrically regular.

Use a deliberately selected set of modes. The visual quality should come from their spatial arrangement, phase relationships, and relative amplitudes—not from adding hundreds of arbitrary oscillators.

The field must remain temporally coherent. Mode phases should evolve continuously, and amplitude changes should be smoothed and damped.

### A crucial design choice

Do not define the field only as a sum of axis-aligned sine waves. That approach is easy to implement but tends to produce generic grids, checkerboards, and repetitive stripes.

Prefer a combination of:
- Curved spatial coordinates.
- Differently oriented mode families.
- A small number of asymmetric spatial terms.
- Distinct wavelengths and phase relationships.
- Slow, bounded evolution of selected parameters.

The field should possess enough structure that its contours create an identifiable sculpture rather than an interchangeable procedural pattern.

## 5. Turning the field into geometry

Prototype three representations before committing to the final rendering strategy.

### Representation A: nested isosurfaces

An isosurface is a surface defined by a constant field value:

\[
F(\mathbf{x},t)=c
\]

Generate several carefully selected isovalues \(c\), producing nested curved surfaces through the field.

Advantages:
- The surfaces follow the field's natural spatial structure.
- Changes in the field can create visually meaningful topological changes.
- Multiple isovalues reveal nodes, lobes, pockets, and interference patterns.

Risks:
- Full volumetric sampling and mesh extraction may be expensive.
- Rebuilding topology every frame can cause flicker, popping, and CPU overhead.
- Excessively dense extraction can produce noisy or visually confusing geometry.

Investigate marching cubes, marching tetrahedra, or other isosurface techniques as references, but do not assume that CPU mesh extraction every frame is acceptable for live performance.

A better initial experiment may use a fixed topology, GPU deformation, or a reduced-resolution field. Dynamic topology should only be introduced if it produces a substantial visual benefit.

### Representation B: contour sheets

Instead of extracting a complete volumetric isosurface, construct parameterized sheets that sample the field along their surface.

For example, a sheet may have a base position \(\mathbf{P}(u,v)\) and a displacement along its normal:

\[
\mathbf{P}'(u,v,t)=
\mathbf{P}(u,v)+
\mathbf{N}(u,v)\,D(u,v,t)
\]

Where \(D\) depends on the field, its gradient, or a selected combination of modes.

The sheets can curve, fold, and develop contour-like bands as the field changes. Several differently oriented sheets may intersect or weave through one another.

Advantages:
- Stable topology.
- Predictable vertex counts.
- Efficient GPU deformation.
- Easier control over composition and silhouette.

Risks:
- Can look like generic animated planes.
- Intersections may produce distracting artifacts.
- Requires strong initial shape design.

If this method is used, give the sheets meaningful curvature and composition before adding field-driven deformation.

### Representation C: hybrid field sculpture

Combine a small number of designed surfaces with field-derived contour bands or limited isosurface geometry.

For example:
- Two or three large curved surfaces define the primary silhouette.
- The field deforms those surfaces and controls their spatial patterns.
- A few contour structures emerge around high-gradient regions or stable nodal boundaries.
- Secondary bands create depth and reveal interference.

This is the recommended starting hypothesis because it combines artistic control with mathematical behavior.

The agent must compare all three using actual rendered prototypes. Choose the approach that creates the best image at acceptable cost, not the one that sounds most mathematically sophisticated.

## 6. Surface hierarchy and nodal structures

The scene must have a hierarchy of spatial features.

### Large scale
Large curved forms, lobes, and broad openings establish the silhouette. These should evolve slowly and respond primarily to lower-frequency energy and persistent modes.

### Medium scale
Nested contours and interference regions create the scene's identity. Their shapes should be driven by the relationship between several modes, not merely by a high-frequency displacement function.

### Fine scale
Narrow ridges, small contour bands, and subtle surface ripples reveal detail. They should remain subordinate to the larger structures.

Use gradients, field magnitude, field derivatives, or selected modal contributions to identify areas that deserve stronger shading or additional contour emphasis.

For example, regions where the field changes rapidly may receive a narrow highlight. Stable nodal regions may remain darker or acquire a subtle contrasting color. Constructive interference may create a broad, luminous lobe.

These are artistic mappings rather than universal physical rules. Tune them to the image and keep the mapping consistent.

Do not outline every triangle, every isosurface, or every mathematical contour. Too much line detail will destroy the sense of sculpture.

## 7. Resonance and musical behavior

The field must respond musically without losing its mathematical identity.

Reuse the existing engine's persistent resonance modes, envelopes, and audio analysis if available.

### Suggested mappings

| Input | Spatial response |
|---|---|
| Bass/sub energy | Broad lobes expand and contract; large surface curvature changes slowly. |
| Low mids | Major contours shift position and neighboring surfaces move into different relationships. |
| Midrange | Interference bands travel through the structure; medium-scale lobes change shape. |
| High frequencies | Fine contour ridges, narrow highlights, and subtle local deformation become more active. |
| Transients | A localized wavefront passes through the field and excites several nearby regions. |
| Sustained tones | Stable modal patterns accumulate and become legible as standing-wave structures. |
| Harmonic content | Multiple modes reinforce or oppose one another, changing the arrangement of contours. |
| Silence | The sculpture retains a restrained, slowly evolving state instead of freezing or becoming visually empty. |

Avoid mapping raw audio amplitude directly to every vertex. Audio should excite the field's existing modes; the field should determine the spatial response.

### Example behavior A: standing-wave emergence

A sustained note gradually strengthens a set of stable contours. Several lobes become visible as their associated surfaces separate or brighten. When the note fades, the structures decay smoothly rather than disappearing abruptly.

### Example behavior B: traveling interference

A transient excites a traveling mode. A luminous or geometric wavefront moves through the field, briefly altering the relationship between neighboring surfaces. The response decays over time and distance.

### Example behavior C: modal exchange

As different frequency bands become prominent, the relative strengths of two or three modes change. One family of contours becomes dominant while another recedes. The entire scene evolves without abruptly replacing its geometry.

### Example behavior D: high-frequency detail

Treble energy activates fine contour detail and narrow highlights. The main silhouette remains stable. The scene should not become a vibrating mesh or a noisy collection of stripes.

### Example behavior E: phrase-scale evolution

Over several seconds, the mode balance and selected spatial phases gradually evolve. Open regions become more enclosed, or broad lobes develop additional subdivisions. This should feel like the field changing state, not a preset transition.

Use smooth attack, release, damping, and bounded modulation. Preserve stable phases and avoid uncontrolled random state changes.

## 8. Lighting and materials

The field should be visible through its surfaces, not through a uniform glow.

Use:
- A dark environment.
- Grazing illumination to reveal curvature.
- Cool rim highlights along selected contours.
- Restrained emissive bands in active interference regions.
- Subtle iridescence where viewing angle and surface orientation justify it.
- Carefully controlled transparency, if needed to reveal overlapping layers.
- Modest bloom on the brightest accents.

Surface shading should help the viewer distinguish neighboring sheets and understand their spatial relationships.

Do not make every surface transparent. Too many overlapping translucent layers can create excessive overdraw and visual muddiness. Begin with mostly opaque or semi-opaque materials and use transparency selectively.

Use the existing renderer's material and post-processing facilities. Do not introduce a separate rendering stack for the scene.

## 9. Camera and environment

The camera should travel through or around the sculpture slowly enough that its spatial structure remains readable.

- Establish a clear initial composition.
- Use controlled forward movement, slow lateral drift, or a gentle orbit.
- Reveal openings and overlaps through parallax.
- Avoid rapid spins, aggressive zooms, and camera motion tied directly to every beat.
- Keep the sculpture large enough to dominate the frame.
- Preserve dark regions that communicate depth.
- Avoid the appearance of a tiny object floating in an empty cinematic void.

The environment should support the artwork rather than compete with it. A restrained background gradient, very subtle atmospheric depth, or sparse distant accents may help, but the surfaces must remain the focus.

## 10. Live controls

Expose a compact set of controls using AV Gen's existing scene parameter and modulation systems:

- **Resonance:** overall modal response.
- **Field Scale:** scale of the dominant spatial patterns.
- **Contour Density:** number or spacing of visible contour families.
- **Interference:** strength of cross-mode interaction.
- **Surface Relief:** geometric displacement.
- **Detail:** fine-scale contour and ripple strength.
- **Spectral Color:** intensity of field-dependent color variation.
- **Light Response:** highlight and emission sensitivity.
- **Motion Scale:** global temporal rate.

Use sensible defaults and bounded ranges. Avoid exposing implementation details that do not translate into useful artistic controls.

MIDI should be able to control a few meaningful parameters through existing engine mechanisms. A single knob should be able to produce an obvious, musically useful transformation.

## 11. Performance and implementation constraints

The scene is intended for live rendering. Optimize the field representation and rendering path before increasing visual complexity.

Investigate:
- Static topology with GPU-side deformation.
- Analytic field evaluation in vertex or compute shaders.
- Reusing field calculations across geometry and shading where practical.
- Reduced-resolution volumetric sampling if volumetric data is required.
- Avoiding full-resolution field recomputation when the image does not benefit.
- Avoiding CPU readback from GPU-computed fields.
- Avoiding full mesh regeneration every frame.
- Limiting transparent layers and overdraw.
- Stable normals and deformation bounds.
- Profiling shader instruction cost, vertex count, and post-processing.

If isosurface extraction proves too expensive, do not abandon the visual idea immediately. Try a hybrid approach with static topology, fewer surfaces, or contour-sheet deformation.

If a full three-dimensional field is unnecessary to achieve the intended image, use a lower-dimensional or analytic representation. The viewer cares about the resulting sculpture, not the computational purity of the simulation.

The reference live target is 120 Hz (8.33 ms per frame), but report actual measured CPU and GPU timings at a specified resolution and hardware configuration. Do not claim the target is met without measurements.

## 12. Implementation stages

### Stage A — Engine audit and three prototypes

1. Inspect the existing resonance and renderer systems.
2. Build a basic nested-isosurface prototype, a contour-sheet prototype, and a hybrid prototype.
3. Use the same initial palette and comparable framing so the approaches can be judged fairly.
4. Capture still images and short motion sequences.

**Gate:** choose the approach with the strongest silhouette, most compelling spatial structure, and acceptable performance. Do not proceed simply because one prototype was easiest to code.

### Stage B — Sculptural field design

1. Establish the large-scale spatial modes.
2. Tune contour spacing and mode relationships.
3. Introduce controlled asymmetry and depth.
4. Refine the composition and camera.

**Gate:** the scene should already look distinctive with neutral shading and no post-processing.

### Stage C — Resonant animation

1. Connect existing audio signals and persistent modes.
2. Implement standing-wave patterns and traveling responses.
3. Add phrase-scale evolution.
4. Test silence, sustained tones, transients, and dense music.

**Gate:** different musical conditions create different spatial states without losing continuity.

### Stage D — Materials and lighting

1. Establish surface shading and grazing highlights.
2. Add field-aware color and restrained emission.
3. Evaluate transparency and iridescence.
4. Add post-processing only when it provides a measurable visual improvement.

**Gate:** the image reads as dimensional mathematical sculpture, not a topographic map or generic shader demo.

### Stage E — Optimization and integration

1. Profile CPU/GPU cost.
2. Optimize the actual bottleneck.
3. Register the scene and expose its controls.
4. Verify normal scene loading, parameter automation, and MIDI integration.
5. Remove unused prototypes and temporary code.
6. Document the final implementation and any limitations.

## 13. Acceptance criteria

Waveform Dimension is ready for review when:

1. The scene has a recognizable sculptural identity independent of its colors.
2. Multiple curved surfaces form a coherent spatial composition.
3. Interference and contour structures are visually legible rather than uniform stripes.
4. The geometry evolves continuously and does not flicker or pop distractingly.
5. Audio excites persistent modes rather than directly driving arbitrary per-frame changes.
6. Bass, midrange, treble, and transients produce distinguishable responses.
7. The scene looks attractive during silence and sustained music.
8. The color palette, shading, and illumination reveal depth without excessive glow.
9. The implementation reuses the engine's existing resonance infrastructure wherever practical.
10. The scene is available through AV Gen's normal scene workflow.
11. Performance is measured on available target hardware with resolution and CPU/GPU timings recorded.
12. The agent provides comparative prototype captures, a summary of the chosen geometry representation, changed-file list, and remaining visual weaknesses.

## 14. Deliverables

- A working Waveform Dimension scene.
- Three early geometry prototypes or documented evidence of their comparison.
- A visual reference board explaining the chosen art direction.
- A brief audit of the existing resonance infrastructure and its reuse.
- Source code and required assets.
- Captures of idle, standing-wave, traveling-interference, and dense-audio states.
- CPU/GPU performance measurements.
- A concise list of known limitations and the next highest-value improvements.

The goal is not to visualize a mathematical equation as literally as possible. The goal is to turn coherent resonance into an evolving, immersive, spatial artwork. Mathematical structure should be the source of its beauty, not an excuse for generic procedural geometry.

---

## Owner correction (2026-10-16): replaces the §3 palette

Here's the corrected art direction:

Obsidian violet
#080713 · Background, deep shadows, negative space

Blackened indigo
#21103D · Main shadow-side surfaces

Royal violet
#482080 · Midtone contours and layered depth

Electric purple
#782A9B · Resonant bands and interference regions

Crimson magenta
#C32965 · Stronger wavefronts and active folds

Spectral red
#F04455 · Selective resonant highlights

Rose-white
#FF8B9A · Rare high-intensity accents

Updated color behavior for Waveform Dimension
- Indigo and violet dominate the large surfaces, preserving depth and making the sculpture feel immersive.
- Red and crimson emerge selectively where modes reinforce one another or interference patterns become intense.
- Pink highlights trace particular contours, rather than illuminating every edge.
- Dark regions remain genuinely dark. The scene should not become a uniformly glowing purple object.
- Color changes should arise from resonance, spatial position, and illumination—not automatic rainbow cycling.
The overall image should feel like red spectral energy trapped inside a vast indigo-violet mathematical sculpture
