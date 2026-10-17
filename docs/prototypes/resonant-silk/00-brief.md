# AV Gen Scene: Resonant Silk
## Art direction, resonance behavior, implementation plan, and acceptance criteria

(Owner brief, received 2026-10-16, saved verbatim. Coordinator notes: the owner has said to ignore the 120 Hz / 8.33 ms reference in these briefs — the live target is 1080p at 30 fps; live scenes carry no audio file — test with assets/audio/feline-footwear.wav fed on the command line.)

### Mission

Create a new, production-quality, live-performance scene for AV Gen called **Resonant Silk**.

The scene depicts enormous continuous sheets and ribbons of luminous, iridescent material suspended in a deep, dark, seemingly boundless space. The forms fold, twist, stretch, overlap, and form elegant passages through space. Waves travel along the material. Folds tighten and release. Large-scale deformations respond to sustained musical energy, while finer ridges and light patterns respond to rhythm and higher-frequency content.

The target is an abstract audiovisual artwork with the visual sophistication of a high-end motion-design or experimental VFX piece—not a generic procedural demo.

It must be visually attractive when the input is silent, musically expressive when audio is present, and performant enough for live use.

### 1. Begin with two independent art agents

Start **two new, independent art agents**, one for Resonant Silk and one for Resonant Lattice. They must work on separate scene branches, worktrees, or otherwise isolated changes so their experiments do not conflict.

This agent owns Resonant Silk.

Before implementation:

1. Read the scene architecture, scene registration conventions, renderer/material APIs, parameter/modulator plumbing, and existing scene examples.
2. Inspect Cymatic World and the engine's resonance implementation.
3. Produce a short visual research board with at least three distinct compositions and a brief technical plan.
4. Implement a minimal, attractive prototype before building a complex scene.
5. Record baseline and prototype performance on the available target hardware.

Do not spend the entire task writing a plan. Move from research to a rendered prototype early.

### 2. Existing engine resonance: investigate before adding anything

I believe AV Gen already has a resonance concept. Verify this in the actual codebase before designing a replacement.

Trace the existing path from audio analysis through the signal bus, timeline/modulators, scene parameters, and rendering. Inspect Cymatic World's implementation to identify which parts are reusable.

Determine whether the current system already supports:

- Persistent resonant modes or oscillators with stable phase.
- Multiple modes with different frequencies, amplitudes, and spatial patterns.
- Attack, decay, damping, sustain, and energy accumulation.
- Excitation by frequency bands, RMS, transients, and beat/onset events.
- Mode mixing and spatial field evaluation.
- MIDI control and automation.
- Deterministic playback or reproducible state.
- Safe parameter smoothing and stable behavior during silence.

Document what exists, what is missing, and what is merely scene-specific. Reuse existing infrastructure wherever practical. Extend it only where a concrete gap is demonstrated.

Do not create a second audio analysis pipeline or a competing global resonance manager.

### 3. Visual identity

**Primary image:** two or three enormous continuous silk-like surfaces sweeping through the frame, with elegant folds and overlapping layers. They should look like a sculptor has created a single coherent object that is being animated by music.

The scene should suggest:

- Liquid silk stretched between invisible anchors.
- Aurora-like sheets that fold into sculptural volumes.
- Iridescent membranes catching narrow strips of light.
- Waves traveling along folds, sometimes converging into luminous creases.
- An immense object that cannot be understood from a single angle.

This is not a pile of independent ribbons. The surfaces should share a coherent spatial design and feel like parts of one artwork.

#### Composition

- Establish one dominant, beautiful silhouette that reads immediately.
- Use one primary surface, then introduce one or two secondary layers only if they improve depth and composition.
- Create an intentional focal region and a clear foreground/midground/background hierarchy.
- Let some forms approach the camera while others recede into darkness.
- Use negative space to reveal the silhouette and prevent the image becoming an opaque tangle.
- Compose for a 16:9 performance output, but avoid making the scene dependent on a single narrow camera angle.
- Keep the camera stable or slowly drifting. No uncontrolled spinning or constant aggressive zooms.
- Make the opening frame immediately attractive, with enough visible structure to communicate the scene's identity.

The result should feel expansive, tactile, elegant, and slightly otherworldly.

### 4. Color palette

Begin with this restrained palette. Treat these as starting sRGB/hex art-direction references, not values to inject unconverted into a linear-light shader.

- **Abyss:** #080B20 — dominant dark background.
- **Midnight indigo:** #171D55 — shadow-side surface color.
- **Electric cyan:** #39E6F2 — principal cool highlight.
- **Glacial teal:** #48CDBB — secondary cool color.
- **Ultraviolet:** #7857F5 — deeper iridescent transition.
- **Hot magenta:** #F04CCF — rare warm accent.
- **Pearl:** #D9F7FF — restrained high-intensity highlights.

The primary look is cyan/teal and indigo/violet, with magenta appearing selectively at interference regions, tight folds, and moments of stronger excitation.

Do not distribute all colors equally. Use large cool color regions, gradients that follow the surface, and small concentrated accents. The palette must remain recognizable across time.

Avoid:
- Rainbow gradients that sweep uniformly across the entire object.
- Full-surface white emission.
- Oversaturated pink and cyan everywhere at once.
- A generic neon wireframe aesthetic.
- A noisy, speckled texture used to disguise weak geometry.

### 5. Geometry: the central technical challenge

Prototype a continuous parameterized surface before considering complicated mesh-generation techniques.

A useful starting representation is a ribbon parameterized by longitudinal coordinate `u` and transverse coordinate `v`. Construct its position from a centerline, a transported local frame, and a width profile. Use several coherent spatial modes to displace the surface along its local normal and tangent directions.

Conceptually:

`P(u,v,t) = C(u,t) + v W(u,v,t) + N(u,v,t) D(u,v,t)`

Where:
- `C` defines the broad centerline or surface path.
- `W` defines the cross-sectional width direction.
- `N` defines a stable local surface normal.
- `D` is the resonant deformation field.

This is a conceptual decomposition, not a requirement to use these exact function names.

#### Required geometric qualities

- Large, smooth folds visible in silhouette.
- Variable width, including elegant tapers and broad sheet-like regions.
- Controlled twists that do not produce sudden frame flips.
- Secondary ripples that follow the larger deformation rather than fighting it.
- Continuous surfaces without visible seams, gaps, or random disconnected pieces.
- Smooth normals and enough tessellation to prevent visible faceting.
- Stable deformation at different frame rates.

Use a small number of coherent, hand-designed spatial modes to establish the base shape. Add low-amplitude procedural variation only after the base composition looks good.

Avoid simply adding multiple sine waves to a flat plane and calling the result silk. The geometry must develop recognizable folds, depth, and a compelling silhouette.

Potential approach:
1. Build a broad, sculptural ribbon or sheet with a hand-authored parameterization.
2. Add two or three independent spatial deformation modes.
3. Add traveling waves with distinct wavelengths and speeds.
4. Add small-scale surface ripples only where they enhance the lighting.
5. Validate the normals and frame construction under maximum deformation.

If the first procedural form looks like a twisting strip of pasta, reject it and change the underlying composition rather than compensating with lighting.

### 6. Resonance model and musical behavior

Use persistent modes, not arbitrary per-frame noise.

Each mode should have a stable identity, phase, frequency, damping, spatial influence, and excitation behavior. If the existing engine already supplies these, consume them.

Use the following examples as initial art direction:

| Musical input | Visual response |
|---|---|
| Sub/bass energy | Broad displacement of the main sheet; slow deep folds expand and contract. |
| Low-mid energy | Large folds travel along the surface; broad curvature changes. |
| Midrange energy | Medium-scale creases and waves propagate across the sheet. |
| High-frequency energy | Fine ripples and narrow highlight bands appear along existing folds. |
| Transient/onset | A wavefront travels away from a point or region, then decays naturally. |
| Sustained note or drone | One or more spatial modes build and settle into a stable, beautiful form. |
| Silence | Motion and illumination settle toward a restrained idle state without abruptly freezing. |

These are examples, not hard-coded assumptions about the engine's band definitions. Inspect the available audio signals and route the best existing ones.

#### Example resonance behaviors

**A. Long-wave excitation:** a kick or low sustained note causes the main sheet to bow outward, with the deformation traveling along its length over several beats.

**B. Fold resonance:** a midrange band strengthens a particular crease pattern. The crease becomes more pronounced, then relaxes smoothly as the energy fades.

**C. Traveling transient:** a snare or onset launches a single traveling ripple. Its energy attenuates with distance and time. It must not produce a permanent global explosion.

**D. Harmonic interference:** two persistent modes overlap and produce moving regions of constructive and destructive displacement. The result should create changing fold relationships, not jitter.

**E. High-frequency shimmer:** treble energy moves a narrow highlight or fine surface ripple without making the entire mesh vibrate.

Use attack/sustain/release behavior and damping so the system feels like a physical resonator. Audio should excite the sculpture, not directly dictate every vertex independently.

Keep response smooth enough to avoid flicker, zippering, or excessive visual fatigue. Provide separate controls for broad deformation and fine detail, even if they share the same underlying audio sources.

### 7. Lighting and material

The surface should read as a thin, glossy, iridescent material rather than glowing plastic.

Use:
- A restrained dark environment.
- Broad cool fill illumination.
- Narrow grazing highlights that reveal curvature.
- Fold-aware illumination or a similar geometry-sensitive shading term.
- View-angle-dependent color shifts where affordable.
- Local emissive accents concentrated along selected creases or wavefronts.
- A restrained bloom pass, if supported by the existing post-processing system.

The surface must retain dark areas. If every region is bright, folds lose their shape and the material becomes flat.

Investigate thin-film iridescence and Fresnel-based approaches, including the MIT-licensed reference implementation:
https://github.com/DerSchmale/threejs-thin-film-iridescence

Use its ideas as a reference rather than introducing Three.js or its rendering stack into the project.

Avoid physically expensive transmission or multiple layers of transparency unless a prototype proves they are necessary. Opaque or mostly opaque surfaces with carefully controlled highlights may produce a more legible and faster live result.

### 8. Motion over time

The scene needs both continuous motion and meaningful state evolution.

- The main silhouette should evolve slowly.
- Traveling waves should move through the object.
- Local folds may tighten, relax, or exchange prominence.
- Light and color should travel in relation to the geometry.
- Different modes should occasionally reinforce each other.
- The scene must not loop obviously every few seconds.

Use deterministic, bounded motion functions and the engine's time/state conventions. No independent random vertex offsets every frame. If noise is used, it should be temporally coherent and low amplitude.

The visual identity should remain intact through quiet and loud passages.

### 9. Live controls

Expose a small, useful set of scene parameters through the existing scene-control and modulation systems:

- **Resonance:** overall modal response.
- **Fold Depth:** broad deformation amplitude.
- **Wave Travel:** traveling-wave strength or speed.
- **Surface Detail:** fine ripple amplitude.
- **Iridescence:** view-dependent color-shift intensity.
- **Light Response:** highlight/emission response.
- **Color Drift:** slow evolution of the color field.
- **Motion Scale:** global motion rate.

Do not create a large collection of near-duplicate controls. Use sensible defaults and bounded ranges. Every control must have an audible and visually meaningful effect.

Support MIDI mapping through the engine's existing mechanisms. Do not add a scene-specific MIDI subsystem.

### 10. Rendering and performance

Prefer GPU-side vertex deformation and shader-driven shading where compatible with the existing renderer. Static topology should be reused. Avoid rebuilding large meshes or uploading all vertex data every frame unless profiling proves that approach appropriate.

Inspect:
- Vertex density and tessellation.
- Normal computation cost.
- Transparency overdraw.
- Bloom and post-processing cost.
- Shader complexity and register pressure.
- Bounds and culling behavior when the surface deforms.

Start with one surface and a modest number of vertices. Add geometry only when it improves the image at performance cost justified by measurement.

Use AV Gen's existing profiler and scene diagnostics. The reference live target is 120 Hz (8.33 ms per frame), but report actual frame and GPU timings rather than claiming the target is met without evidence. Test at representative live output resolution on available Apple Silicon hardware.

Do not rewrite the renderer or introduce a new graphics API for this scene.

### 11. Implementation stages

**Stage A — Engine audit and visual prototype**
- Trace existing resonance and modulation infrastructure.
- Create one continuous surface with a strong silhouette.
- Establish camera, palette, base material, and broad motion.
- Render a static and an animated preview.
- Gate: the form must look compelling before complex effects are added.

**Stage B — Resonant geometry**
- Add persistent modes and traveling waves using the existing engine where possible.
- Connect audio bands and transients.
- Verify stable, smooth response at varied signal levels.
- Gate: distinct musical inputs produce distinct, understandable deformations.

**Stage C — Material and illumination**
- Add fold-sensitive highlights, iridescence, and restrained emission.
- Add post-processing only if it materially improves the result.
- Gate: the material reads as dimensional silk, not flat neon or generic plastic.

**Stage D — Composition and performance**
- Tune camera framing, depth, silhouette, and motion.
- Profile GPU and CPU cost.
- Test silence, sparse audio, sustained audio, transients, and dense music.
- Gate: no distracting artifacts and acceptable live performance.

**Stage E — Integration**
- Register the scene and expose it through the normal scene-selection workflow.
- Add useful default parameter values and documentation.
- Ensure it builds and runs with the normal project configuration.
- Remove prototype-only code and unused assets.

### 12. Acceptance criteria

The scene is ready for review when:

1. Its silhouette and composition are recognizable and compelling without audio.
2. It looks like continuous, sculptural material rather than a collection of independent animated strips.
3. Bass, mids, highs, and transients produce visibly distinct behaviors.
4. Motion is continuous and damped, with no random per-frame geometry changes.
5. The palette is cohesive and the highlights reveal form.
6. The scene works in silence and under varied music.
7. Existing engine resonance and modulation infrastructure is reused wherever practical.
8. The scene is selectable and works through the normal AV Gen scene workflow.
9. Performance results include measured CPU/GPU timings and resolution.
10. The agent provides before/after screenshots, short representative captures if available, a list of changed files, and a concise implementation summary.

### 13. Deliverables

- Working Resonant Silk scene.
- A short visual research board or reference sheet.
- A brief note documenting the existing resonance system and the parts reused.
- Source code and any necessary assets.
- Screenshots showing idle, sustained resonance, and transient response.
- Performance measurements.
- A list of remaining visual weaknesses and the next most valuable improvements.

Do not mark the scene complete merely because it compiles or responds to audio. The primary acceptance criterion is a compelling audiovisual artwork with a clear, distinctive visual identity.
